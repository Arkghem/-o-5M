#include "O5MResourceManager.h"

#include "O5MResource.h"
#include "O5MMeshResource.h"
#include "O5MTextureResource.h"
#include "O5MShaderResource.h"

template<typename T>
T* O5MResourceManager::getResource(const std::string& resourceId) {
    auto& typeResources = resources[std::type_index(typeid(T))];
    auto it = typeResources.find(resourceId);

    if (it != typeResources.end()) {
        return static_cast<T*>(it->second.resource.get());
    }

    return nullptr;
}

template<typename T>
bool O5MResourceManager::hasResource(const std::string& resourceId) {
    auto& typeResources = resources[std::type_index(typeid(T))];
    auto it = typeResources.find(resourceId);

    return it != typeResources.end();
}

template<typename T>
void O5MResourceManager::release(const std::string& resourceId) {
    auto& typeResources = resources[std::type_index(typeid(T))];
    auto it = typeResources.find(resourceId);

    if (it != typeResources.end()) {
        it->second.refCount--;

        if (it->second.refCount <= 0) {
            it->second.resource->unload();
            typeResources.erase(it);
        }
    }
}

template<typename T>
O5MResourceHandle<T> O5MResourceManager::load(const std::string& resourceId) {
    static_assert(std::is_base_of<O5MResource, T>::value);

    O5MResourceHandle<T> handle(resourceId, this);
    auto& typeResources = resources[std::type_index(typeid(T))];
    auto it = typeResources.find(resourceId);

    if (it != typeResources.end()) {
        it->second.refCount++;
    } else {
        auto resource = std::make_shared<T>(resourceId);
        if (!resource->load()) {
            handle = O5MResourceHandle<T>();
        }
        typeResources[resourceId] = { resource, 1 };
    }

    std::string filePath = getFilePath<T>(resourceId);
    try {
        fileTimestamps[filePath] = std::filesystem::last_write_time(filePath);
    } catch (const std:: filesystem::filesystem_error& e) {
        //file doesn't exist
    }

    return handle;
}

void O5MResourceManager::unloadAll(void) {
    for (auto& [type, typeResources] : resources) {
        for (auto& [id, resourceData] : typeResources) {
            resourceData.resource->unload();
        }
        typeResources.clear();
    }
    resources.clear();
}

void O5MResourceManager::startWatcher(void) {
   running = true;
   watcherThread = std::thread([this]() {
        updateWatcherThread();
    });
}

void O5MResourceManager::stopWatcher(void) {
    running = false;
    if (watcherThread.joinable()) {
        watcherThread.join();
    }
}

template<typename T>
std::string O5MResourceManager::getFilePath(const std::string& resourceId) {
    auto& filePattern = pathPattern[std::type_index(typeid(T))];
    return std::get<0>(filePattern) + resourceId + std::get<1>(filePattern);
}

void O5MResourceManager::updateWatcherThread(void) {
    while (running) {
        for (auto& [filePath, timestamp] : fileTimestamps) {
            try {
                auto currentTimestamp = std::filesystem::last_write_time(filePath);
                if (currentTimestamp != timestamp) {
                    timestamp = currentTimestamp;
                    reloadResource(filePath);
                }
            } catch (const std::filesystem::filesystem_error& e) {
                //file doesn't exist
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
void O5MResourceManager::reloadResource(const std::string& filePath) {
    
}
