#ifndef __O5MRESOURCEMANAGER__H
#define __O5MRESOURCEMANAGER__H

#include <unordered_map>
#include <cstdint>
#include <memory>
#include <vector>

#include "Resources/O5MResource.h"

class O5MResourceManager {
public:
    struct Slot{
        std::shared_ptr<O5MResource> resource;
        int refCount = 0;
        bool isPreloaded = false;
        uint32_t generation = 0;
        uint32_t nextFreeIndex = 0xFFFF;
    };
private:
    O5MResourceManager(void) = default;

    uint32_t freeHead = 0xFFFF;

    std::vector<Slot> resources; //use stl vector is definitely a bug to fix, but no need to worry toomuch
    std::unordered_map<uint64_t, uint32_t> idToIndex;
public:
    static O5MResourceManager& getInstance(void) {
        static O5MResourceManager instance;
        return instance;
    }

    bool hasResource(const uint64_t resourceId) const;

    template<typename T>
    O5MResourceHandle<T> acquire(const uint64_t resourceId) {
        static_assert(std::is_base_of<O5MResource, T>::value, "T must be derived of O5MResource");
        if (hasResource(resourceId)) {
            uint32_t tIndex = idToIndex.find(resourceId)->second;
            resources[tIndex].refCount++;
            return O5MResourceHandle<T>(tIndex, resources[tIndex].generation);
        }

        return O5MResourceHandle<T>(0xFFFF, 0);
    }

    template<typename T, typename... Args>
    O5MResourceHandle<T> create(Args&&... args) {
        static_assert(std::is_base_of<O5MResource, T>::value, "T must be derived of O5MResource");

        std::shared_ptr<T> resource = std::make_shared<T>(std::forward<Args>(args)...);

        if (freeHead == 0xFFFF) {
            resources.push_back(Slot {
                    .resource = resource,
                    .refCount = 1,
                    .generation = 1,
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

    Slot& getResource(const uint32_t index);
    void release(const uint64_t resourceId);
};


#endif // !__O5MRESOURCEMANAGER__H
