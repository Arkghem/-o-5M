#include "O5MEntity.h"
#include "O5MComponent.h"

template<typename T, typename... Args>
T* O5MEntity::addComponent(Args&&... args) {
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
T* O5MEntity::getComponent(void) {
    size_t typeID = O5MComponent::getTypeID<T>();
    auto it = componentMap.find(typeID);
    if (it != componentMap.end()) {
        return static_cast<T*>(it->second);
    }
    return nullptr;
}

template<typename T>
bool O5MEntity::removeComponent(void) {
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
