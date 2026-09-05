#ifndef __O5MRESOURCE__H
#define __O5MRESOURCE__H

#include <string>

class O5MResource {
private:
    std::string resourceId;
    bool loaded = false;
public:
    explicit O5MResource(const std::string& id) : resourceId(id) {}
    virtual ~O5MResource() = default;
public:
    const std::string& getId(void) const { return resourceId; }
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


class O5MResourceManager;

template <typename T>
class O5MResourceHandle {
private:
    std::string resourceId;
    O5MResourceManager*  resourceManager;
public:
    O5MResourceHandle(void) : resourceManager(nullptr) {}
    O5MResourceHandle(const std::string& resourceId, O5MResourceManager* resourceManager = nullptr) : 
        resourceId(resourceId), resourceManager(resourceManager) {}
public:
    T* operator->(void) const { return get(); }
    T& operator&(void) const { return *get(); }
    operator bool(void) const { return isValid(); }
public:
    T* get() const;

    bool isValid(void) const;

    const std::string& getId(void) const {
        return resourceId;
    }
};
#endif //!__O5MRESOURCE__H
