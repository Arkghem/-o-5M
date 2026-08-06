#ifndef __CLEARVALUE_H__
#define __CLEARVALUE_H__

// -----------------------------------------------------------------------------
// ClearValue — interface-level clear descriptor (shared by GL and Vulkan)
// -----------------------------------------------------------------------------
// WHY THIS FILE EXISTS:
//   ClearValue is passed through ICommandBuffer::beginPass(), which lives in the
//   RHI interface. Originally the struct was defined inline in the GL backend
//   (core/opengl/GLCommandBuffer.h) using GLfloat/GLint — an interface leak:
//   the Vulkan backend would have had to include a GL header just to call
//   beginPass(). Moving it here puts the contract at the interface layer where
//   it belongs, and all backends include the same definition.
//
// WHY float/int IS LAYOUT-COMPATIBLE WITH VkClearValue:
//   VkClearValue is a C union of { VkClearColorValue color;   // 4 x float
//                                 VkClearDepthStencilValue depthStencil; } // float + uint
//   A C struct with members (float[4], float, int) has the same layout as that
//   union's two members side by side, so a backend can memcpy this struct into
//   VkClearValue (or reinterpret it) without manual conversion — color lands in
//   VkClearValue.color, depth lands in depthStencil.depth, stencil in
//   depthStencil.stencil. GL has no equivalent "clear value struct" concept,
//   the backend just forwards each field to glClearBufferfv/glClearBufferiv.
//
// active == false SEMANTICS:
//   A pass may only need to clear depth, not color. Instead of picking "magic"
//   values, `active` explicitly says "don't clear this attachment". The Vulkan
//   backend maps this to loadOp = LOAD (preserve previous contents) instead of
//   loadOp = CLEAR; the GL backend simply skips the glClearBuffer* call. This
//   mirrors VkRenderPassAttachmentBeginInfo semantics where clearing is a
//   loadOp decision, not a value decision.
struct ClearValue {
    bool  active;       // if false → skip clear (Vulkan: loadOp=LOAD)
    float color[4];     // RGBA clear color; layout-compatible with VkClearValue.color
    float depth;        // depth clear value; layout-compatible with VkClearValue.depthStencil.depth
    int   stencil;      // stencil clear value; layout-compatible with VkClearValue.depthStencil.stencil
};

#endif //__CLEARVALUE_H__
