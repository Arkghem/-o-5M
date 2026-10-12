#ifndef __O5MENTITYHANDLE__H
#define __O5MENTITYHANDLE__H

#include <cstddef>
#include <cstdint>
#include <functional>

struct O5MEntityHandle {
    // 0xFFFFFFFF, not 0: index 0 is a valid entity.
    static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

    uint32_t index = kInvalidIndex;
    uint32_t generation = 0;   // 0 == never issued

    O5MEntityHandle(void) = default;
    O5MEntityHandle(uint32_t index, uint32_t generation)
        : index(index), generation(generation) {}

    bool isValid(void) const { return index != kInvalidIndex; }
    explicit operator bool(void) const { return isValid(); }

    uint32_t getIndex(void) const { return index; }
    uint32_t getGeneration(void) const { return generation; }

    friend bool operator==(const O5MEntityHandle&, const O5MEntityHandle&) = default;
};

// Lets handles key an unordered_map.
template <>
struct std::hash<O5MEntityHandle> {
    size_t operator()(const O5MEntityHandle& handle) const noexcept {
        const uint64_t packed =
            (static_cast<uint64_t>(handle.generation) << 32) | handle.index;
        return std::hash<uint64_t>{}(packed);
    }
};

#endif // !__O5MENTITYHANDLE__H
