#ifndef __O5MWORLD__H
#define __O5MWORLD__H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Scene/O5MEntity.h"
#include "Scene/O5MEntityHandle.h"

class O5MWorld {
private:
    // Either occupied (entity != nullptr) or on the free list.
    struct Slot {
        std::unique_ptr<O5MEntity> entity;
        uint32_t generation = 0;
        uint32_t nextFreeIndex = O5MEntityHandle::kInvalidIndex;
    };

    // Marks a pass over the entity set; destroy requests are queued instead of
    // executed, since the doomed entity may be the one running right now.
    struct IterationGuard {
        O5MWorld& world;
        bool wasIterating;

        explicit IterationGuard(O5MWorld& world)
            : world(world), wasIterating(world.iterating) {
            world.iterating = true;
        }
        ~IterationGuard(void) {
            if (wasIterating) {
                return;   // nested pass: iterating is already true, outermost drains
            }
            // Keep deferring while draining: a hook may ask for more destroys,
            // and re-entering destroySlot on the entity being torn down would
            // free it from under its own destroy() frame.
            world.iterating = true;
            world.flushPendingDestroy();
            world.iterating = false;
        }
        IterationGuard(const IterationGuard&) = delete;
        IterationGuard& operator=(const IterationGuard&) = delete;
    };

    std::vector<Slot> slots;
    uint32_t freeHead = O5MEntityHandle::kInvalidIndex;
    size_t entityCount = 0;

    std::vector<O5MEntityHandle> pendingDestroy;
    bool iterating = false;
    bool initialized = false;

    double elapsedTime = 0.0;      // scaled game time, seconds
    uint64_t frameIndex = 0;
    float lastDeltaTime = 0.0f;    // dt handed to entities last frame
    float timeScale = 1.0f;
    float maxDeltaTime = 0.1f;     // longer steps are treated as a hitch
    bool paused = false;
public:
    O5MWorld(void) = default;
    ~O5MWorld(void);

    O5MWorld(const O5MWorld&) = delete;
    O5MWorld& operator=(const O5MWorld&) = delete;
    O5MWorld(O5MWorld&&) = delete;
    O5MWorld& operator=(O5MWorld&&) = delete;

    O5MEntityHandle createEntity(const std::string& name);

    // Safe to call from inside update() / forEachEntity(): teardown is queued
    // until the pass is over. Invalid, stale or repeated destroys are no-ops.
    void destroyEntity(O5MEntityHandle handle);

    // nullptr when the handle is invalid, stale or out of range.
    O5MEntity* getEntity(O5MEntityHandle handle);
    const O5MEntity* getEntity(O5MEntityHandle handle) const;
    bool isAlive(O5MEntityHandle handle) const;

    size_t getEntityCount(void) const { return entityCount; }
    size_t getSlotCount(void) const { return slots.size(); }

    // The entity set is snapshotted: entities created from inside fn are not
    // visited by this call.
    template<typename F>
    void forEachEntity(F&& fn) {
        IterationGuard guard(*this);
        const size_t count = slots.size();
        for (size_t i = 0; i < count; ++i) {
            if (slots[i].entity) {
                fn(*slots[i].entity);
            }
        }
    }

    void init(void);
    void update(float rawDeltaTime);
    void render(void);

    void setTimeScale(float scale) { timeScale = scale; }
    float getTimeScale(void) const { return timeScale; }

    void setMaxDeltaTime(float seconds) { maxDeltaTime = seconds; }
    float getMaxDeltaTime(void) const { return maxDeltaTime; }

    void setPaused(bool isPaused) { paused = isPaused; }
    bool isPaused(void) const { return paused; }

    double getElapsedTime(void) const { return elapsedTime; }
    uint64_t getFrameIndex(void) const { return frameIndex; }
    float getDeltaTime(void) const { return lastDeltaTime; }
private:
    Slot* resolve(O5MEntityHandle handle);
    const Slot* resolve(O5MEntityHandle handle) const;

    void destroySlot(uint32_t index);
    void flushPendingDestroy(void);

    static uint32_t nextGeneration(uint32_t generation);
};

#endif // !__O5MWORLD__H
