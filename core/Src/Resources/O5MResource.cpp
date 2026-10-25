#include "Resources/O5MResource.h"
#include "Resources/O5MResourceManager.h"

#include <cassert>

O5MResource* __resource_resolve(uint32_t index, uint32_t generation) {
    if (index == 0xFFFF) {
        return nullptr;
    }

    auto& resourceManager = O5MResourceManager::getInstance();
    if (index >= resourceManager.getResourceCount()) {
        return nullptr;
    }
    auto& slot = resourceManager.getResource(index);

    return slot.generation == generation ? slot.resource.get() : nullptr;
}
