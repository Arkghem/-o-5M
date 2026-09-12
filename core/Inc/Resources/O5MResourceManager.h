#ifndef __O5MRESOURCEMANAGER__H
#define __O5MRESOURCEMANAGER__H

#include <unordered_map>
#include <string>
#include <cstdint>
#include <filesystem>
#include <thread>

#include "Resources/O5MResource.h"

class O5MResourceManager {
public:
    struct Slot{
        std::shared_ptr<O5MResource> resource;
        int refCount;
        bool isPreloaded;
        uint32_t generation;
        uint32_t nextFreeIndex;
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
    O5MResourceHandle<T> acquire(const uint64_t resourceId);

    template<typename T, typename... Args> 
    O5MResourceHandle<T> create(Args&&... args);

    Slot& getResource(const uint32_t index);
    void release(const uint64_t resourceId);
};


#endif // !__O5MRESOURCEMANAGER__H
