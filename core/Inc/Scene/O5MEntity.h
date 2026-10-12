#ifndef __O5MENTITY__H
#define __O5MENTITY__H

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Scene/O5MComponent.h"
#include "Scene/O5MEntityHandle.h"

class O5MComponent;
class O5MWorld;

class O5MEntity {
    // Only the world owns the slot table and the generation counters, so only
    // the world may construct, initialize or tear an entity down.
    friend class O5MWorld;
public:
    O5MEntity(void) = delete;
    O5MEntity(const O5MEntity&) = delete;
    O5MEntity& operator=(const O5MEntity&) = delete;
    ~O5MEntity(void) = default;

    const O5MEntityHandle& getHandle(void) const { return handle; }

    const std::string& getName(void) const { return name; }
    void setName(const std::string& name) { this->name = name; }

    // A disabled entity skips update() and render() without being destroyed.
    bool isActive(void) const { return active; }
    void setActive(bool isActive) { this->active = isActive; }

    void init(void) {
        if (initialized) {
            return;
        }
        initialized = true;
        for (auto& component : components) {
            component->init();
        }
    }

    // deltaTime is measured once by the frame loop and passed down unchanged;
    // clamping / pausing / scaling happen in O5MWorld::update.
    void update(float deltaTime) {
        if (!active) {
            return;
        }
        for (auto& component : components) {
            if (component->isActive()) {
                component->update(deltaTime);
            }
        }
    }

    void render(void) {
        if (!active) {
            return;
        }
        for (auto& component : components) {
            if (component->isActive()) {
                component->render();
            }
        }
    }

    // One component per type: adding twice returns the existing one.
    template<typename T, typename... Args>
    T* addComponent(Args&&... args) {
        static_assert(std::is_base_of<O5MComponent, T>::value, "T must derive from O5MComponent");
        size_t typeID = O5MComponent::getTypeID<T>();

        auto it = componentMap.find(typeID);
        if (it != componentMap.end()) {
            return static_cast<T*>(it->second);
        }

        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        component->setOwner(this);
        T* raw = component.get();
        components.push_back(std::move(component));
        componentMap[typeID] = raw;

        // A component added after the world was initialized must not stay
        // Uninitialized: bring it up right away.
        if (initialized) {
            raw->init();
        }
        return raw;
    }

    template<typename T>
    T* getComponent(void) {
        size_t typeID = O5MComponent::getTypeID<T>();
        auto it = componentMap.find(typeID);
        if (it != componentMap.end()) {
            return static_cast<T*>(it->second);
        }
        return nullptr;
    }

    template<typename T>
    bool removeComponent(void) {
        size_t typeID = O5MComponent::getTypeID<T>();
        auto it = componentMap.find(typeID);
        if (it != componentMap.end()) {
            O5MComponent* compPtr = it->second;
            componentMap.erase(it);

            for (auto compIt = components.begin(); compIt != components.end(); ++compIt) {
                if (compIt->get() == compPtr) {
                    // Run the hook while the object is still alive.
                    compPtr->destroy();
                    components.erase(compIt);
                    return true;
                }
            }
        }
        return false;
    }
private:
    // Private: only O5MWorld constructs an entity, passing the handle it just
    // allocated, so identity and storage can never disagree.
    O5MEntity(O5MEntityHandle handle, const std::string& name)
        : handle(handle), name(name) {}

    // Called by O5MWorld before the slot is released. Reverse creation order:
    // a later component may hold references into an earlier one.
    void destroy(void) {
        for (auto it = components.rbegin(); it != components.rend(); ++it) {
            (*it)->destroy();
        }
    }

    O5MEntityHandle handle;
    std::string name;
    bool active = true;
    bool initialized = false;

    std::vector<std::unique_ptr<O5MComponent>> components;
    std::unordered_map<size_t, O5MComponent*> componentMap;
};

#endif // !__O5MENTITY__H
