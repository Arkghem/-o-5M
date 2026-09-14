// mesh_verify — MeshResource 几何加载验证（无窗口，Phase 0 练习 3 验收靶场）
//
// 测试输入：手工构造的 GLB（内存内生成，无外部资产）——
//   两个 primitive、各 4 顶点/6 索引的 quad：
//     prim0: uint16 索引，quad x∈[-1,0]
//     prim1: uint16 索引，quad x∈[2,3]（验收多 primitive 的顶点偏移）
//   属性：POSITION/NORMAL(vec3)/TANGENT(vec4)/TEXCOORD_0(vec2)，无 COLOR_0
//     （覆盖默认色 = 白 的路径）
//
// 验证链路：GLB -> tinygltf -> Vertex[]/u32[] -> staging -> device buffer
//   -> copy 回 HOST_VISIBLE -> 逐字段比对（pos/norm/tangent/uv/color + 索引序列）
//
// 预期抓到的缺陷（修复前本测试应 FAIL）：
//   1. 16 位索引（componentType 5123）被 throw —— 练习 3 的核心要求
//   2. 多 primitive 的索引未加顶点偏移 —— 期望全局索引序列
//      [0,1,2,0,2,3, 4,5,6,4,6,7]，未偏移会得到 [0,1,2,0,2,3, 0,1,2,0,2,3]
//
// 退出码：0 = 通过；非 0 = 失败。

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "Resources/O5MMeshResource.h"
#include "RHI/O5MDevice.h"

namespace {

#ifdef __APPLE__
const std::vector<const char*> kInstanceExtensions = {
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
};
const std::vector<const char*> kDeviceExtensions = {
    "VK_KHR_portability_subset",
    VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME
};
#else
const std::vector<const char*> kInstanceExtensions = {};
const std::vector<const char*> kDeviceExtensions = {
    VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME
};
#endif

// ---------------------------------------------------------------------------
// 最小 GLB 构造器（little-endian；Apple Silicon / x86 均为 LE，直接 memcpy）
// ---------------------------------------------------------------------------

void putU16(std::vector<uint8_t>& out, uint16_t v) {
    out.insert(out.end(), reinterpret_cast<uint8_t*>(&v),
               reinterpret_cast<uint8_t*>(&v) + 2);
}
void putU32(std::vector<uint8_t>& out, uint32_t v) {
    out.insert(out.end(), reinterpret_cast<uint8_t*>(&v),
               reinterpret_cast<uint8_t*>(&v) + 4);
}
void putF32(std::vector<uint8_t>& out, float v) {
    putU32(out, *reinterpret_cast<uint32_t*>(&v));
}

// BIN 布局（全部 4 字节对齐，总 408B）：
//   [  0,  12) idx0  6*u16    [204, 216) idx1  6*u16
//   [ 12,  60) pos0 4*vec3    [216, 264) pos1 4*vec3
//   [ 60, 108) nrm0 4*vec3    [264, 312) nrm1 4*vec3
//   [108, 172) tan0 4*vec4    [312, 376) tan1 4*vec4
//   [172, 204) uv0  4*vec2    [376, 408) uv1  4*vec2
const char* kJson =
    "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
    "\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
    "\"meshes\":[{\"primitives\":["
    "{\"attributes\":{\"POSITION\":1,\"NORMAL\":2,\"TANGENT\":3,\"TEXCOORD_0\":4},\"indices\":0},"
    "{\"attributes\":{\"POSITION\":6,\"NORMAL\":7,\"TANGENT\":8,\"TEXCOORD_0\":9},\"indices\":5}"
    "]}],"
    "\"buffers\":[{\"byteLength\":408}],"
    "\"bufferViews\":["
    "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":12,\"target\":34963},"
    "{\"buffer\":0,\"byteOffset\":12,\"byteLength\":48,\"target\":34962},"
    "{\"buffer\":0,\"byteOffset\":60,\"byteLength\":48},"
    "{\"buffer\":0,\"byteOffset\":108,\"byteLength\":64},"
    "{\"buffer\":0,\"byteOffset\":172,\"byteLength\":32},"
    "{\"buffer\":0,\"byteOffset\":204,\"byteLength\":12,\"target\":34963},"
    "{\"buffer\":0,\"byteOffset\":216,\"byteLength\":48,\"target\":34962},"
    "{\"buffer\":0,\"byteOffset\":264,\"byteLength\":48},"
    "{\"buffer\":0,\"byteOffset\":312,\"byteLength\":64},"
    "{\"buffer\":0,\"byteOffset\":376,\"byteLength\":32}"
    "],"
    "\"accessors\":["
    "{\"bufferView\":0,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"},"
    "{\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\",\"min\":[-1,-1,0],\"max\":[0,1,0]},"
    "{\"bufferView\":2,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
    "{\"bufferView\":3,\"componentType\":5126,\"count\":4,\"type\":\"VEC4\"},"
    "{\"bufferView\":4,\"componentType\":5126,\"count\":4,\"type\":\"VEC2\"},"
    "{\"bufferView\":5,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"},"
    "{\"bufferView\":6,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\",\"min\":[2,-1,0],\"max\":[3,1,0]},"
    "{\"bufferView\":7,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
    "{\"bufferView\":8,\"componentType\":5126,\"count\":4,\"type\":\"VEC4\"},"
    "{\"bufferView\":9,\"componentType\":5126,\"count\":4,\"type\":\"VEC2\"}"
    "]}";

std::vector<uint8_t> buildTestGlb() {
    std::vector<uint8_t> bin;
    bin.reserve(408);

    const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };

    auto emitIndices = [&](const uint16_t* is) {
        for (int i = 0; i < 6; ++i) putU16(bin, is[i]);
    };
    auto emitQuad = [&](float x0) {
        // pos: 4 顶点 (x0,-1,0)(x0+1,-1,0)(x0+1,1,0)(x0,1,0)
        const float px[4] = { x0, x0 + 1, x0 + 1, x0 };
        const float py[4] = { -1.f, -1.f, 1.f, 1.f };
        for (int v = 0; v < 4; ++v) { putF32(bin, px[v]); putF32(bin, py[v]); putF32(bin, 0.f); }
        // normal: (0,0,1) * 4
        for (int v = 0; v < 4; ++v) { putF32(bin, 0.f); putF32(bin, 0.f); putF32(bin, 1.f); }
        // tangent: (1,0,0,1) * 4   （glTF TANGENT 是 vec4，w=手性）
        for (int v = 0; v < 4; ++v) { putF32(bin, 1.f); putF32(bin, 0.f); putF32(bin, 0.f); putF32(bin, 1.f); }
        // uv: (0,0)(1,0)(1,1)(0,1)
        const float u[4] = { 0.f, 1.f, 1.f, 0.f };
        const float w[4] = { 0.f, 0.f, 1.f, 1.f };
        for (int v = 0; v < 4; ++v) { putF32(bin, u[v]); putF32(bin, w[v]); }
    };

    emitIndices(idx);        // [0,12)
    emitQuad(-1.f);          // [12,204)
    emitIndices(idx);        // [204,216)
    emitQuad(2.f);           // [216,408)

    // GLB 组装：header + JSON chunk + BIN chunk
    std::vector<uint8_t> glb;
    const size_t jsonLen = std::strlen(kJson);
    const size_t jsonPadded = (jsonLen + 3) & ~size_t(3);
    const uint32_t total =
        12u + uint32_t(8 + jsonPadded) + uint32_t(8 + bin.size());

    putU32(glb, 0x46546C67);            // magic 'glTF'
    putU32(glb, 2);                     // version
    putU32(glb, total);

    putU32(glb, uint32_t(jsonPadded));  // JSON chunk
    putU32(glb, 0x4E4F534A);            // 'JSON'
    glb.insert(glb.end(), kJson, kJson + jsonLen);
    glb.insert(glb.end(), jsonPadded - jsonLen, 0x20);  // 空格补齐到 4B

    putU32(glb, uint32_t(bin.size()));  // BIN chunk
    putU32(glb, 0x004E4942);            // 'BIN\0'
    glb.insert(glb.end(), bin.begin(), bin.end());
    return glb;
}

uint32_t findGraphicsQueueFamily(const vk::PhysicalDevice& physicalDevice) {
    auto props = physicalDevice.getQueueFamilyProperties();
    for (uint32_t i = 0; i < props.size(); ++i) {
        if (props[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            return i;
        }
    }
    throw std::runtime_error("no graphics queue family found");
}

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

bool feq(float a, float b) { return std::fabs(a - b) < 1e-5f; }

} // namespace

int main() {
    try {
        // ---- bootstrap（与 tex_verify 相同）----
        vk::raii::Context context;
        vk::ApplicationInfo appInfo("o5m-mesh-verify", 1, "o5m", 1, VK_API_VERSION_1_2);

        std::vector<const char*> validationLayers;
        for (const auto& layer : context.enumerateInstanceLayerProperties()) {
            if (std::string_view(layer.layerName) == "VK_LAYER_KHRONOS_validation") {
                validationLayers.push_back("VK_LAYER_KHRONOS_validation");
                std::cout << "[ok] validation layer found\n";
                break;
            }
        }
        const bool useValidation = !validationLayers.empty();

        vk::InstanceCreateInfo instanceInfo;
#ifdef __APPLE__
        instanceInfo.setFlags(vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR);
#endif
        instanceInfo.setPApplicationInfo(&appInfo)
                    .setPEnabledLayerNames(validationLayers)
                    .setPEnabledExtensionNames(kInstanceExtensions);
        vk::raii::Instance instance(context, instanceInfo);

        vk::raii::PhysicalDevices physicalDevices(instance);
        const vk::raii::PhysicalDevice& physicalDevice = physicalDevices.front();
        const uint32_t graphicsFamily = findGraphicsQueueFamily(*physicalDevice);
        float queuePriority = 1.0f;
        vk::DeviceQueueCreateInfo queueInfo({}, graphicsFamily, 1, &queuePriority);
        vk::DeviceCreateInfo deviceInfo;
        vk::PhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures(true);
        deviceInfo.setQueueCreateInfos(queueInfo)
                  .setPEnabledExtensionNames(kDeviceExtensions)
                  .setPNext(&dynamicRenderingFeatures);
        O5MDevice o5mDevice(physicalDevice, vk::raii::Device(physicalDevice, deviceInfo),
                            graphicsFamily);
        std::cout << "[ok] device ready (" << physicalDevice.getProperties().deviceName << ")\n";

        // ---- 被测对象：手工 GLB（双 primitive + uint16 索引）----
        std::vector<uint8_t> glb = buildTestGlb();
        O5MMeshResource meshRes("test-quad.glb", o5mDevice);
        meshRes.setData(glb.data(), glb.size());

        bool loaded = false;
        try {
            loaded = meshRes.load();
        } catch (const std::exception& e) {
            std::cerr << "[FAIL] load() threw: " << e.what()
                      << "  <-- 16-bit index (5123) support missing?\n";
            return 1;
        }
        CHECK(loaded, "load() succeeds on uint16-indexed GLB");
        if (!loaded) return 1;

        CHECK(meshRes.getVertexCount() == 8, "vertexCount == 8 (2 quads)");
        CHECK(meshRes.getIndexCount() == 12, "indexCount == 12");
        std::cout << "[ok] loaded: " << meshRes.getVertexCount() << " verts, "
                  << meshRes.getIndexCount() << " indices\n";

        // ---- 读回 device buffer 比对 ----
        vk::raii::CommandPool commandPool(o5mDevice.getDevice(), { {}, graphicsFamily });
        vk::raii::CommandBuffers cmds(
            o5mDevice.getDevice(), { *commandPool, vk::CommandBufferLevel::ePrimary, 1 });

        using V = O5MMeshResource::Vertex;
        const vk::DeviceSize vSize = meshRes.getVertexCount() * sizeof(V);
        const vk::DeviceSize iSize = meshRes.getIndexCount() * sizeof(uint32_t);

        auto [vbStag, vbMem] = o5mDevice.createBuffer(
            vSize, vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        auto [ibStag, ibMem] = o5mDevice.createBuffer(
            iSize, vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

        auto& cmd = cmds[0];
        cmd.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        cmd.copyBuffer(meshRes.getVertexBuffer(), *vbStag, vk::BufferCopy(0, 0, vSize));
        cmd.copyBuffer(meshRes.getIndexBuffer(), *ibStag, vk::BufferCopy(0, 0, iSize));
        cmd.end();

        vk::SubmitInfo submit;
        submit.setCommandBuffers(*cmd);
        vk::raii::Fence fence(o5mDevice.getDevice(), vk::FenceCreateInfo());
        (void)o5mDevice.getQueue().submit(submit, *fence);
        if (o5mDevice.getDevice().waitForFences(*fence, true, UINT64_MAX) !=
            vk::Result::eSuccess) {
            throw std::runtime_error("waitForFences failed");
        }

        const V* verts = static_cast<const V*>(vbMem.mapMemory(0, vSize));
        const uint32_t* idxOut = static_cast<const uint32_t*>(ibMem.mapMemory(0, iSize));

        // 索引序列：prim1 的局部索引必须 +4（多 primitive 顶点偏移）
        const uint32_t wantIdx[12] = { 0,1,2,0,2,3, 4,5,6,4,6,7 };
        for (int i = 0; i < 12; ++i) {
            if (idxOut[i] != wantIdx[i]) {
                std::cerr << "[FAIL] index[" << i << "] got " << idxOut[i]
                          << " want " << wantIdx[i]
                          << "  <-- multi-primitive vertex offset missing?\n";
                ++g_failures;
            }
        }
        ++g_checks;

        // 顶点字段：prim0 v0 与 prim1 v0（含默认白颜色路径）
        const V& v0 = verts[0];
        CHECK(feq(v0.m_pos.x, -1.f) && feq(v0.m_pos.y, -1.f) && feq(v0.m_pos.z, 0.f),
              "v0 position");
        CHECK(feq(v0.m_norm.x, 0.f) && feq(v0.m_norm.y, 0.f) && feq(v0.m_norm.z, 1.f),
              "v0 normal (0,0,1)");
        CHECK(feq(v0.m_tangent.x, 1.f) && feq(v0.m_tangent.y, 0.f) && feq(v0.m_tangent.z, 0.f),
              "v0 tangent xyz (1,0,0) — vec4 的 w 被丢弃是预期行为");
        CHECK(feq(v0.m_texCoord.x, 0.f) && feq(v0.m_texCoord.y, 0.f), "v0 texCoord");
        CHECK(feq(v0.m_color.x, 1.f) && feq(v0.m_color.y, 1.f) && feq(v0.m_color.z, 1.f),
              "v0 color defaults to white (no COLOR_0)");

        const V& v4 = verts[4];
        CHECK(feq(v4.m_pos.x, 2.f) && feq(v4.m_pos.y, -1.f) && feq(v4.m_pos.z, 0.f),
              "v4 position (prim1 base)");
        CHECK(feq(v4.m_norm.z, 1.f), "v4 normal");
        CHECK(feq(v4.m_texCoord.x, 0.f) && feq(v4.m_texCoord.y, 0.f), "v4 texCoord");

        vbMem.unmapMemory();
        ibMem.unmapMemory();

        if (g_failures == 0) {
            std::cout << "[PASS] mesh_verify: " << g_checks << " checks OK"
                      << " (uint16 indices + multi-primitive offset + attributes)\n";
            return 0;
        }
        std::cout << "[FAIL] " << g_failures << "/" << g_checks << " checks failed\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << "\n";
        return 1;
    }
}
