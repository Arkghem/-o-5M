# Vulkan Backend Code Review — Issues

## Minor Issues

### IS-01: VK_CHECK inconsistency
**Severity**: Low | **Files**: VKRhi.cpp, VKMemoryAllocator.cpp, VKPipeline.cpp, VKShader.cpp

9 out of 32 vkCreate*/vkAllocate* calls use manual error checks instead of VK_CHECK:
- VKRhi.cpp: vkCreateCommandPool, vkAllocateCommandBuffers, vkCreateFence, vkCreateSemaphore (×2)
- VKMemoryAllocator.cpp: vkAllocateMemory
- VKPipeline.cpp: vkCreatePipelineLayout
- VKShader.cpp: vkCreateShaderModule

Justification: These functions return error codes (bool/empty-struct) to callers
instead of asserting — VK_CHECK's assert(false) is not appropriate here.

### IS-02: Dead local variable in VKPipeline.cpp:321
`float blendConstants[4]` declared but never used. VkPipelineColorBlendStateCreateInfo's
blendConstants member is assigned directly. The local array is dead code.

## No Critical Issues Found

- No raw pointer leaks
- No missing destructor cleanup
- No dangling Vk* handles
- No header guard violations
