#include "Resources/O5MResourceManager.h"

#include "Resources/O5MResource.h"

template<typename T>
O5MResourceHandle<T> O5MResourceManager::acquire(const uint64_t resourceId) {
    static_assert(std::is_base_of<O5MResource, T>::value, "T must be derived from O5MResource");
    if (hasResource(resourceId)) {
        uint32_t tIndex = idToIndex.find(resourceId)->second;
        resources[tIndex].refCount++;
        return O5MResourceHandle<T>{
            .index = tIndex,
            .generation = resources[idToIndex[resourceId]].generation
        };
    }
    
    //invalid resourceId
    return O5MResourceHandle<T> {
        .index = 0xFFFF,
        .generation = 0
    };
}

template<typename T, typename... Args> 
O5MResourceHandle<T> O5MResourceManager::create(Args&&... args) {
    static_assert(std::is_base_of<O5MResource, T>::value, "T must be derived from O5MResource");

    std::shared_ptr<T> resource = std::make_shared<T>(std::forward<Args>(args)...);
    
    if (freeHead == 0xFFFF) {
        resources.push_back(Slot {
                .generation = 1,
                .resource = resource,
                .refCount = 1,
                .nextFreeIndex = 0xFFFF,
                });

        idToIndex[resource->getResourceId()] = resources.size() - 1;

        return O5MResourceHandle<T>(resources.size() - 1, 1);
    }

    Slot& candidateSlot = resources.at(freeHead);
    candidateSlot.generation++;
    candidateSlot.resource = resource;
    candidateSlot.refCount = 1;

    idToIndex[resource->getResourceId()] = freeHead;

    O5MResourceHandle<T> res(freeHead, candidateSlot.generation);

    freeHead = candidateSlot.nextFreeIndex;

    return res;
}

O5MResourceManager::Slot& O5MResourceManager::getResource(const uint32_t index) {
    return resources[index];
}

bool O5MResourceManager::hasResource(const uint64_t resourceId) const {
    auto it = idToIndex.find(resourceId);
    return it != idToIndex.end();
}

void O5MResourceManager::release(const uint64_t resourceId) {
    auto it = idToIndex.find(resourceId);

    if (it != idToIndex.end()) {
        auto& slot = getResource(it->second);

        slot.resource->unload();
        slot.generation++;
        slot.nextFreeIndex = freeHead;
        freeHead = it->second;

        idToIndex.erase(it);
    }
}
