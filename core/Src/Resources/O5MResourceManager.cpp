#include "Resources/O5MResourceManager.h"

#include "Resources/O5MResource.h"

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
