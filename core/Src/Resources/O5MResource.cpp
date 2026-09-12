#include "Resources/O5MResource.h"
#include "Resources/O5MResourceManager.h"

#include <cassert>

O5MResource* __resource_resolve(uint32_t index, uint32_t generation) {
    auto& resourceManager = O5MResourceManager::getInstance();
    auto& slot = resourceManager.getResource(index);

    return slot.generation == generation ? slot.resource.get() : nullptr;
}
