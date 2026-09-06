#include "O5MResourceManager.h"

#include "O5MResource.h"

template<typename T>
T* O5MResourceManager::getResource(const uint32_t resourceId) {
    auto it = resources.find(resourceId);
    if (it != resources.end()) {
        return static_cast<T*>(it->second.resource.get());
    }

    return nullptr;
}

bool O5MResourceManager::hasResource(const uint32_t resourceId) {
    auto it = resources.find(resourceId);
    return it != resources.end();
}

void O5MResourceManager::release(const uint32_t resourceId) {
    auto it = resources.find(resourceId);

    if (it != resources.end()) {
        it->second.refCount--;

        if (it->second.refCount <= 0) {
            it->second.resource->unload();
            resources.erase(it);
        }
    }
}

template<typename T>
uint32_t O5MResourceManager::load(const std::string& filePath) {
    static_assert(std::is_base_of<O5MResource, T>::value);
    
    auto it = pathToId.find(filePath);
    uint32_t resourceId;
    if (it != pathToId.end()) {
        resources[it->second].refCount++;
        resourceId = it->second;
    } else {
        auto resource = std::make_shared<T>(filePath);
        if (!resource->load()) {
            throw std::runtime_error("Failed to load resource: " + filePath);
        }
        resourceId = allocateId();
        resources[resourceId] = { resource, 1 };
    }

    try {
        fileTimestamps[resourceId] = std::filesystem::last_write_time(filePath);
    } catch (const std:: filesystem::filesystem_error& e) {
        //file doesn't exist
    }

    return resourceId;
}

void O5MResourceManager::unloadAll(void) {
    for (auto& [id, resourceData] : resources) {
        resourceData.resource->unload();
    }
    
    nextId = 1;

    pathToId.clear();
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

void O5MResourceManager::updateWatcherThread(void) {
    while (running) {
        for (auto& [resourceId, timestamp] : fileTimestamps) {
            try {
                auto it = resources.find(resourceId);
                if (it != resources.end()) {
                    const std::string& filePath = it->second.resource->getfilePath();
                    auto currentTimestamp = std::filesystem::last_write_time(filePath);
                    if (currentTimestamp != timestamp) {
                        timestamp = currentTimestamp;
                        reloadResource(resourceId);
                    }
                }
            } catch (const std::filesystem::filesystem_error& e) {
                //file doesn't exist
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
void O5MResourceManager::reloadResource(const uint32_t resourceId) {
    auto it = resources.find(resourceId);
    if (it != resources.end()) {
        try {
            it->second.resource->unload();
            it->second.resource->load();
        } catch (const std::exception& e) {
            throw std::runtime_error("reload resource failed at" + it->second.resource->getfilePath());
        }
    }
}
