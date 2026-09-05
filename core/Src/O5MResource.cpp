#include "O5MResource.h"
#include "O5MResourceManager.h"

template<typename T>
T* O5MResourceHandle<T>::get(void) const {
    return resourceManager->getResource<T>(resourceId);
}

template<typename T>
bool O5MResourceHandle<T>::isValid(void) const {
    return resourceManager->getResource<T>(resourceId) != nullptr;
}
