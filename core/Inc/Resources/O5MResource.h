#ifndef __O5MRESOURCE__H
#define __O5MRESOURCE__H

#include <cstdint>
#include <atomic>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

class O5MResource {
private:
    static inline std::atomic<uint64_t> nextId = 1;

    uint64_t resourceId;
    std::string name;
    void* data = nullptr;
    size_t size = 0;
    std::vector<uint8_t> ownedData;

    bool loaded = false;

public:
    explicit O5MResource(const std::string& name):
        name(name), 
        resourceId(nextId.fetch_add(1, std::memory_order_relaxed))
    {}

    // `data` may point into `ownedData`; copying the object would leave that
    // pointer aimed at the source object's vector. Resources are manager-owned
    // and referenced through handles, so copies/moves are not part of the API.
    O5MResource(const O5MResource&) = delete;
    O5MResource& operator=(const O5MResource&) = delete;
    O5MResource(O5MResource&&) = delete;
    O5MResource& operator=(O5MResource&&) = delete;

    virtual ~O5MResource() = default;

public:
    bool isloaded(void) const { return loaded; }
    uint64_t getResourceId(void) const { return resourceId; }
    std::string getName(void) const { return name; }
    void setData(void* data, size_t byteSize) {
        this->data = data;
        this->size = byteSize;
    }

    // Owns the source bytes. Use this for data produced by an importer, whose
    // temporary parser buffers die before the resource does.
    void setData(std::vector<uint8_t> bytes) {
        ownedData = std::move(bytes);
        setData(ownedData.data(), ownedData.size());
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
    uint32_t index = 0xFFFF;
    uint32_t generation = 0;

    O5MResource* resolove(void) const;

public:
    O5MResourceHandle(void) = default;
    [[nodiscard]]O5MResourceHandle(uint32_t index, uint32_t generation) : 
        index(index), generation(generation) {}
    O5MResourceHandle(const O5MResourceHandle&) = default;
    O5MResourceHandle(O5MResourceHandle&&) = default;
    O5MResourceHandle& operator=(const O5MResourceHandle&) = default;
    O5MResourceHandle& operator=(O5MResourceHandle&&) = default;

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
