#include "Resources/O5MResource.h"
#include "Resources/O5MResourceManager.h"

#include <cassert>

template <typename T>
T* O5MResourceHandle<T>::get(void) const {
    static_assert(std::is_base_of<O5MResource, T>::value, "T must be derived from O5MResource");

    auto& resourceManager = O5MResourceManager::getInstance();
    auto& slot = resourceManager.getResource(index);

    return slot.generation == generation ? std::static_pointer_cast<T>(slot.resource).get() : nullptr;
}
