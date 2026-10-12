#ifndef __O5MENTITY__H
#define __O5MENTITY__H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

#include "Scene/O5MComponent.h"

class O5MComponent;
class O5MEntity {
private:
    std::string name;
    bool active = true;

    std::vector<std::unique_ptr<O5MComponent>> components;
    std::unordered_map<size_t, O5MComponent*> componentMap;
public:
    explicit O5MEntity(const std::string& entityName) : name(entityName) {}

    const std::string& getName(void) const { return name; }
    void setName(const std::string& name) { this->name = name; }

    bool isActive(void) const { return active; }
    void setActive(bool isActive) { active = isActive; }

    void init(void);
    void update(void);
    void render(void);

    template<typename T, typename... Args>
    T* addComponent(Args&&... args) {
        static_assert(std::is_base_of<O5MComponent, T>::value, "" );
        size_t typeID = O5MComponent::getTypeID<T>();

        auto it = componentMap.find(typeID);
        if (it != componentMap.end()) {
            return static_cast<T*>(it->second);
        }
        
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        component->setOwner(this);
        components.push_back(std::move(component));
        componentMap[typeID] = components.back().get();
        return static_cast<T*>(componentMap[typeID]);


    }

    template<typename T>
    T* getComponent(void) {
        size_t typeID = O5MComponent::getTypeID<T>();
        auto it = componentMap.find(typeID);
        if (it != componentMap.end()) {
            return static_cast<T*>(it->second);
        }
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
                    components.erase(compIt);
                    return true;
                }
            }
        }
        return false;
    }
};

#endif // !__O5MENTITY__H
