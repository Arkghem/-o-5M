#ifndef __O5MRESOURCE__H
#define __O5MRESOURCE__H

#include <cstdint>
#include <string>
#include <fstream>

#include "O5MResourceManager.h"

class O5MResource {
protected:
    std::string readFile(const std::string& filePath) {
        std::ifstream file(filePath, std::ios::ate | std::ios::binary);

        if (!file.is_open()) {
            throw std::runtime_error("failed to open file: " + filePath);
        }

        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();
        return std::string(buffer.begin(), buffer.end());
    }
private:
    std::string m_filePath;
    bool loaded = false;

public:
    explicit O5MResource(const std::string filePath) : m_filePath(filePath) {}
    virtual ~O5MResource() = default;

public:
    const std::string& getfilePath(void) const { return m_filePath; }
    bool isloaded(void) const { return loaded; }
public:
    // call virtual function for specific loading and unloading
    bool load(void) {
        loaded = doLoad();
        return loaded;
    }; 
    void unload(void) {
        doUnload();
        loaded = false;
    };
protected:
    virtual bool doLoad(void) = 0;
    virtual void doUnload(void) = 0;
};

template <typename T>
class O5MResourceHandle {
private:
    O5MResourceManager&  resourceManager = O5MResourceManager::getInstance();
    uint32_t resourceId = 0;
public:
    O5MResourceHandle(void) = delete;
    [[nodiscard]] O5MResourceHandle(const std::string& filePath);
public:
    T* operator->(void) const { return get(); }
    T& operator&(void) const { return *get(); }
    operator bool(void) const { return isValid(); }
public:
    T* get() const;

    bool isValid(void) const { return resourceId == 0; };

    const uint32_t getId(void) const {
        return resourceId;
    }
};

#endif //!__O5MRESOURCE__H
