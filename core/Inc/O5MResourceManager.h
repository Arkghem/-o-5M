#ifndef __O5MRESOURCEMANAGER__H
#define __O5MRESOURCEMANAGER__H

#include <unordered_map>
#include <string>
#include <cstdint>
#include <filesystem>
#include <thread>

class O5MResource;

class O5MResourceManager {
private:
    O5MResourceManager(void) = default;

    struct O5MResourceData {
        std::shared_ptr<O5MResource> resource;
        int refCount;
        bool isPreloaded;
    };

    std::unordered_map<uint32_t, O5MResourceData> resources;
    std::unordered_map<std::string, uint32_t> pathToId;

    // --Multithread part
    std::unordered_map<uint32_t, std::filesystem::file_time_type> fileTimestamps;
    std::thread watcherThread;
    std::atomic<bool> running = false;

    // --Allocate id part
    std::atomic<uint32_t> nextId = 1;
public:
    static O5MResourceManager& getInstance(void) {
        static O5MResourceManager instance;
        return instance;
    }

    uint32_t allocateId(void) {
        return nextId.fetch_add(1, std::memory_order_relaxed);
    }

    template<typename T>
    T* getResource(const uint32_t resourceId) ;

    bool hasResource(const uint32_t resourceId);

    void release(const uint32_t resourceId);    

    template<typename T>
    uint32_t load(const std::string& filePath);

    void unloadAll(void);

    void startWatcher(void);
    void stopWatcher(void);
private:
    void updateWatcherThread(void);
    void reloadResource(const uint32_t resourceId);
};


#endif // !__O5MRESOURCEMANAGER__H
