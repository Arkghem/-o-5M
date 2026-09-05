#ifndef __O5MRESOURCEMANAGER__H
#define __O5MRESOURCEMANAGER__H

#include <unordered_map>
#include <typeindex>
#include <string>
#include <filesystem>
#include <thread>

#include "O5MResource.h"

class O5MResourceManager {
private:
    struct O5MResourceData {
        std::shared_ptr<O5MResource> resource;
        int refCount;
        bool isPreloaded;
    };

    std::unordered_map<std::type_index,
                       std::unordered_map<std::string, O5MResourceData>> resources;
    std::unordered_map<std::type_index, std::tuple<std::string, std::string>> pathPattern; //register sheet

    // --Multithread part
    std::unordered_map<std::string, std::filesystem::file_time_type> fileTimestamps;
    std::thread watcherThread;
    std::atomic<bool> running = false;
public:
    template<typename T>
    T* getResource(const std::string& resourceId) ;

    template<typename T>
    bool hasResource(const std::string& resourceId);

    //why it doesn't a template
    //it think it need one
    template<typename T>
    void release(const std::string& resourceId);    

    template<typename T>
    O5MResourceHandle<T> load(const std::string& resourceId);

    void unloadAll(void);

    void startWatcher(void);
    void stopWatcher(void);
private:
    template<typename T>
    std::string getFilePath(const std::string& resourceId);
    void updateWatcherThread(void);
    void reloadResource(const std::string& filePath);
};


#endif // !__O5MRESOURCEMANAGER__H
