#ifndef __O5MRESOURCE__H
#define __O5MRESOURCE__H

#include <cstdint>
#include <atomic>
#include <string>


class O5MResource {
private:
    static inline std::atomic<uint64_t> nextId = 1;

    uint64_t resourceId;
    std::string name;
    void* data;
    size_t size;

    bool loaded = false;

public:
    explicit O5MResource(const std::string& name):
        name(name), 
        resourceId(nextId.fetch_add(1, std::memory_order_relaxed))
    {}

    virtual ~O5MResource() = default;

public:
    bool isloaded(void) const { return loaded; }
    uint64_t getResourceId(void) const { return resourceId; }
    std::string getName(void) const { return name; }
    void setData(void* data, size_t byteSize) {
        this->data = data;
        this->size = byteSize;
    }

    void* getData(void) const { return data; }
    size_t getSize(void) const { return size; }

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

O5MResource* __resource_resolve(uint32_t index, uint32_t generation);

template <typename T>
class O5MResourceHandle {
private:
    const uint32_t index = 0xFFFF;
    uint32_t generation = 0;

    O5MResource* resolove(void) const;

public:
    O5MResourceHandle(void) = default;
    [[nodiscard]]O5MResourceHandle(uint32_t index, uint32_t generation) : 
        index(index), generation(generation) {}

    T* operator->(void) const { return get(); }
    T& operator*(void) const { return *get(); }
    operator bool(void) const { return isValid(); }

public:
    T* get() const {
        static_assert(std::is_base_of<O5MResource, T>::value, "T must be derived from O5MResource");

        return static_cast<T*>(__resource_resolve(index, generation));
    };

    bool isValid(void) const { return index != 0xFFFF; };
};

#endif //!__O5MRESOURCE__H
