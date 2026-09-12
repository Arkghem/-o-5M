// resmgr_cycle_test — Phase 0 练习 1 验收：ResourceManager 全生命周期
//
// 纯 CPU 单元测试（无 Vulkan）：FakeResource 走
//   create → load → acquire → acquire(不存在) → release → 再 create(free-list 复用)
// 并验证 generation 悬垂拒绝语义。
//
// 退出码：0 = 全部通过；非 0 = 失败（失败项打印到 stderr，带行号）。
// 运行：cmake --build build --target resmgr_cycle_test && ./build/resmgr_cycle_test
//   或 ctest --test-dir build -R resmgr

#include <cstdint>
#include <iostream>
#include <string>

#include "Resources/O5MResource.h"
#include "Resources/O5MResourceManager.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool cond, const char* msg, int line) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::cerr << "[FAIL] line " << line << ": " << msg << "\n";
    }
}
#define CHECK(cond, msg) check((cond), (msg), __LINE__)

// 最小假资源：只数 load/unload 次数，验证生命周期回调
class FakeResource : public O5MResource {
public:
    explicit FakeResource(const std::string& name) : O5MResource(name) {}
    int loads = 0;
    int unloads = 0;
protected:
    bool doLoad() override { ++loads; return true; }
    void doUnload() override { ++unloads; }
};

} // namespace

int main() {
    auto& rm = O5MResourceManager::getInstance();

    // ---- 场景 1：create → 句柄与登记 ----
    auto ha = rm.create<FakeResource>(std::string("fake-A"));
    CHECK(ha.isValid(), "create returns valid handle");
    CHECK(ha.get() != nullptr, "create handle resolves");
    CHECK(ha->getName() == "fake-A", "handle operator-> works");
    const uint64_t idA = ha->getResourceId();
    CHECK(rm.hasResource(idA), "idToIndex populated after create");

    // ---- 场景 2：load ----
    CHECK(ha->load(), "load succeeds");
    CHECK(ha->loads == 1, "doLoad called exactly once");
    CHECK(ha->isloaded(), "loaded flag set");

    // ---- 场景 3：同 id 二次 acquire ----
    auto ha2 = rm.acquire<FakeResource>(idA);
    CHECK(ha2.isValid(), "acquire existing id -> valid handle");
    CHECK(ha2.get() != nullptr, "acquire existing id resolves");
    CHECK(ha2->getName() == "fake-A", "acquire resolves to the same resource");

    // ---- 场景 4：acquire 不存在的 id（失败句柄不得崩溃）----
    auto hbad = rm.acquire<FakeResource>(0xDEADBEEF);
    CHECK(!hbad.isValid(), "acquire unknown id -> invalid handle");
    CHECK(hbad.get() == nullptr, "acquire unknown id -> nullptr, no OOB crash");

    // ---- 场景 5：release → 悬垂拒绝 ----
    // 注意：release 后对象仅由 slot 的 shared_ptr 保活；一旦槽被复用（场景 6）
    // 对象即析构。所以 unload 计数必须在复用前读走。
    FakeResource* rawA = ha.get();
    rm.release(idA);
    CHECK(!rm.hasResource(idA), "idToIndex erased after release");
    const int unloadsA = rawA->unloads;
    CHECK(unloadsA == 1, "unload called exactly once on release");
    CHECK(ha.get() == nullptr, "stale handle rejected (generation mismatch)");
    CHECK(ha2.get() == nullptr, "second stale handle rejected too");
    // 设计语义注记：isValid() 只看 index（"曾经合法"），存活性查询用 get() != nullptr
    CHECK(ha.isValid(), "isValid stays true for stale handle (index-based, documented)");

    // ---- 场景 6：再 create（free-list 槽复用路径）----
    auto hc = rm.create<FakeResource>(std::string("fake-C"));
    CHECK(hc.isValid() && hc.get() != nullptr, "create after release works (slot reuse)");
    CHECK(hc->getName() == "fake-C", "reused slot resolves the new resource");
    CHECK(ha.get() == nullptr, "old generation still rejected after slot reuse");
    CHECK(rm.hasResource(hc->getResourceId()), "new id registered");

    // ---- 场景 7：默认构造句柄 ----
    O5MResourceHandle<FakeResource> hd;
    CHECK(!hd.isValid(), "default-constructed handle is invalid");
    CHECK(hd.get() == nullptr, "default-constructed handle resolves nullptr");

    // ---- 场景 8：多资源共存与选择性释放 ----
    auto hb = rm.create<FakeResource>(std::string("fake-B"));
    CHECK(hb.isValid() && hb->getName() == "fake-B", "second live resource coexists");
    CHECK(hc.get() != nullptr, "earlier resource unaffected by new create");
    rm.release(hb->getResourceId());
    CHECK(hc.get() != nullptr, "releasing B does not disturb C");
    CHECK(unloadsA == 1, "A's unload count unaffected by B/C lifecycle");

    if (g_failures == 0) {
        std::cout << "[PASS] resmgr_cycle_test: " << g_checks << " checks OK\n";
        return 0;
    }
    std::cout << "[FAIL] " << g_failures << "/" << g_checks << " checks failed\n";
    return 1;
}
