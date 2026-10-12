#include "Scene/O5MWorld.h"

#include <algorithm>
#include <cmath>

O5MWorld::~O5MWorld(void) {
    // Same deferral as destroySlot: a hook may destroy further entities.
    IterationGuard guard(*this);
    for (size_t i = 0; i < slots.size(); ++i) {
        if (slots[i].entity) {
            slots[i].entity->destroy();
        }
    }
}

uint32_t O5MWorld::nextGeneration(uint32_t generation) {
    ++generation;
    // Skip 0: it means "never issued" and keeps a default handle from validating.
    return generation == 0 ? 1u : generation;
}

O5MWorld::Slot* O5MWorld::resolve(O5MEntityHandle handle) {
    if (!handle.isValid() || handle.index >= slots.size()) {
        return nullptr;
    }

    Slot& slot = slots[handle.index];
    if (slot.entity == nullptr || slot.generation != handle.generation) {
        return nullptr;   // recycled slot
    }
    return &slot;
}

const O5MWorld::Slot* O5MWorld::resolve(O5MEntityHandle handle) const {
    return const_cast<O5MWorld*>(this)->resolve(handle);
}

O5MEntityHandle O5MWorld::createEntity(const std::string& name) {
    uint32_t index = O5MEntityHandle::kInvalidIndex;

    if (freeHead != O5MEntityHandle::kInvalidIndex) {
        // Recycle, bumping the generation so old handles stay dead.
        index = freeHead;
        Slot& recycled = slots[index];
        freeHead = recycled.nextFreeIndex;
        recycled.nextFreeIndex = O5MEntityHandle::kInvalidIndex;
        recycled.generation = nextGeneration(recycled.generation);
    } else {
        index = static_cast<uint32_t>(slots.size());
        slots.emplace_back();
        slots.back().generation = 1;
    }

    // new instead of make_unique: the latter is not a friend of O5MEntity and
    // cannot reach its private constructor.
    Slot& slot = slots[index];
    slot.entity = std::unique_ptr<O5MEntity>(
        new O5MEntity(O5MEntityHandle(index, slot.generation), name));

    // Entities born after init() still need their onInitializing() hooks, or
    // they stay Uninitialized and their update() is skipped forever.
    if (initialized) {
        slot.entity->init();
    }

    ++entityCount;
    return O5MEntityHandle(index, slot.generation);
}

O5MEntity* O5MWorld::getEntity(O5MEntityHandle handle) {
    Slot* slot = resolve(handle);
    return slot ? slot->entity.get() : nullptr;
}

const O5MEntity* O5MWorld::getEntity(O5MEntityHandle handle) const {
    const Slot* slot = resolve(handle);
    return slot ? slot->entity.get() : nullptr;
}

bool O5MWorld::isAlive(O5MEntityHandle handle) const {
    return resolve(handle) != nullptr;
}

void O5MWorld::destroySlot(uint32_t index) {
    if (slots[index].entity == nullptr) {
        return;
    }

    // The hooks are user code and may destroy further entities -- including this
    // one. The guard queues those requests instead of re-entering here, which
    // would free this entity from under its own destroy() frame.
    IterationGuard guard(*this);

    // No Slot& is held across the hook call: createEntity may reallocate slots.
    slots[index].entity->destroy();

    slots[index].entity.reset();
    slots[index].generation = nextGeneration(slots[index].generation);
    slots[index].nextFreeIndex = freeHead;
    freeHead = index;

    --entityCount;
}

void O5MWorld::destroyEntity(O5MEntityHandle handle) {
    if (resolve(handle) == nullptr) {
        return;
    }

    if (iterating) {
        pendingDestroy.push_back(handle);
        return;
    }

    destroySlot(handle.index);
}

void O5MWorld::flushPendingDestroy(void) {
    // Index loop: nested destroys may still append to the vector.
    for (size_t i = 0; i < pendingDestroy.size(); ++i) {
        const O5MEntityHandle handle = pendingDestroy[i];
        if (resolve(handle) != nullptr) {
            destroySlot(handle.index);
        }
    }
    pendingDestroy.clear();
}

void O5MWorld::init(void) {
    initialized = true;
    IterationGuard guard(*this);
    for (Slot& slot : slots) {
        if (slot.entity) {
            slot.entity->init();
        }
    }
}

void O5MWorld::update(float rawDeltaTime) {
    float deltaTime = rawDeltaTime;
    if (!std::isfinite(deltaTime) || deltaTime < 0.0f) {
        deltaTime = 0.0f;
    }
    // Clamp before scaling, so a time scale can never amplify a stall.
    deltaTime = std::min(deltaTime, maxDeltaTime);
    if (paused) {
        deltaTime = 0.0f;
    }
    deltaTime *= timeScale;

    lastDeltaTime = deltaTime;
    elapsedTime += static_cast<double>(deltaTime);
    ++frameIndex;   // advances while paused too

    IterationGuard guard(*this);
    for (Slot& slot : slots) {
        if (slot.entity) {
            slot.entity->update(deltaTime);
        }
    }
}

void O5MWorld::render(void) {
    IterationGuard guard(*this);
    for (Slot& slot : slots) {
        if (slot.entity) {
            slot.entity->render();
        }
    }
}
