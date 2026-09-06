#include "O5MResource.h"

template<typename T>
O5MResourceHandle<T>::O5MResourceHandle(const std::string& filePath) {
    resourceId = O5MResourceManager::getInstance().load<T>(filePath);
}

template<typename T>
T* O5MResourceHandle<T>::get(void) const {
    return resourceManager.getResource<T>(resourceId);
}
