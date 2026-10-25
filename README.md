# o5m-render

A learning-first Vulkan renderer, written from scratch.

The goal is not to ship an industrial-strength engine, but to implement every layer of a
modern renderer by hand — RHI, render graph, resource management, ECS scene organization —
and to end up with a PBR pipeline whose every line is understood.

> The project started on OpenGL with a hand-rolled RHI layer, then migrated wholesale to
> Vulkan RAII (`vulkan_raii.hpp`). It is developed and verified on macOS through MoltenVK.

**Status:** Phase 0/1/2 are complete; **Phase 3 — geometry pipeline, depth/stencil,
descriptor allocator and ECS wiring — is in progress** on the `Phase_Engine` branch.
Today `o5m_viewer` opens a real-time window, drives an FPS camera from keyboard/mouse and
renders through the render graph with two frames in flight. The geometry path is what
Phase 3 is building: there is no vertex buffer, index buffer or depth attachment in the
viewer yet (the current demo generates its triangle from `gl_VertexIndex`), and the mesh
component is still a placeholder waiting to be wired into the ECS. The frame-local draw
snapshot (`O5MDrawItem`) and frustum culling live in the renderer layer, not in `Scene`.

---

## Table of contents

- [Features](#features)
- [Architecture](#architecture)
- [Frame pipeline](#frame-pipeline)
- [Repository layout](#repository-layout)
- [Build](#build)
- [Running](#running)
- [Roadmap](#roadmap)
- [Design notes and non-goals](#design-notes-and-non-goals)
- [Third-party libraries](#third-party-libraries)
- [Platform notes](#platform-notes)
- [License](#license)

---

## Features

### RHI (`core/Inc/RHI`)

- **Device-layer legislation.** `vkCreateBuffer`, `vkCreateImage` and `vkAllocateMemory`
  are allowed to appear in exactly one file, `core/Src/RHI/O5MDevice.cpp`. Every GPU object
  and every byte of device memory in the engine is created through `O5MDevice`, which makes
  memory accounting and future allocator work tractable.
- **Allocation ledger.** Allocation count is tracked against the reported
  `maxMemoryAllocationCount` (≈4096 on most drivers) and the ceiling is treated as an
  explicit acceptance criterion for a later VMA/sub-allocation migration. For now each
  resource owns its own `vkAllocateMemory` — a deliberate simplification.
- **Dynamic rendering only.** `vkCmdBeginRendering` / `vkCmdEndRendering` through
  `VK_KHR_dynamic_rendering` (enabled on a 1.2 instance, which is what MoltenVK exposes),
  with `vk::raii` handles throughout; there is no legacy `VkRenderPass` / `VkFramebuffer`
  anywhere in the tree.
- **`O5MSwapchain`.** Creation (FIFO present, `minImageCount + 1` images, sRGB format
  preferred), `acquire()` / `present()` with out-of-date and suboptimal handling, and a
  blocking `rebuild()` for resizes; owns the image views aligned with the acquired image
  index.
- **`O5MPipeline`.** A graphics pipeline plus dynamic-rendering `begin()` / `end()` and
  descriptor-set binding. Parameterization (vertex input, depth/stencil state, MRT,
  push constants) is Phase 3 work in flight.

### Render graph (`core/Inc/RenderGraph`)

- **Declarations and physical resources are separate.** A pass only declares what it reads
  and writes plus a recording lambda; `compile()` derives execution order, barriers,
  image layouts, memory and lifetime from those declarations.
- **Typed usage declarations are the single source of truth.** `TexRead::Sampled`,
  `TexWrite::ColorClear` / `Depth` / `Present`, `TexRW::Storage`, `BufRead::Uniform` /
  `VertexIndex`, `BufWrite::TransferDst`, … map to stage masks, access masks, layouts,
  image aspects and usage flags. The same tables drive validation, so an unsupported
  combination fails loudly instead of producing a silently wrong barrier.
- **Compute passes are first-class citizens** (`PassKind::Compute` has its own
  stage/access mapping, and a compute pass is added with `addComputePass`). No compute pass
  is exercised yet — the first real consumer arrives with the Phase 4 mipmap generation and
  the Phase 5 research work — but the graph already models them rather than being retrofitted
  later.
- **Two-round simulation, two baked barrier plans.** History reads depend on state that a
  *previous frame* left behind and therefore does not exist at compile time. The frame
  timeline is periodic, so `compile()` simulates the schedule twice: round 0 starts from
  initial states (→ `firstFrameBarrier`, replayed on frame 0) and round 1 continues from
  round 0's end states (→ `steadyBarrier`, replayed on every later frame).
- **Ping-pong history resources.** A resource declared with `readHistory` is materialized
  as two physical instances; `O5MRenderContext` exposes current/history without callers
  branching on it (from frame 1 onwards, frame 0's history side is undefined by design).
- **Import of external resources.** `importTexture()` / `importBuffer()` reference raw
  handles the graph never owns — the swapchain backbuffer enters the graph this way — and
  the declared usages are validated against the real ones.
- **Dead-pass culling.** `markOutput()` names the resources that must actually be produced;
  everything not reachable upstream from those roots is removed from the execution order.
- **Cycle detection with a readable report.** A cycle throws and lists the passes sitting
  on it.
- **Lifecycle and usage inference.** First/last-use slots are computed per resource and the
  individual usage bits are merged, so a created resource is allocated with exactly the
  usage its passes need.
- **`dump()`** prints the compile result — execution order, per-pass declarations,
  lifecycle table and the baked barrier plans — which is how the graph is debugged.

### Resources (`core/Inc/Resources`)

- **Handle-based lifetime.** `O5MResourceHandle<T>` is a slot index plus a generation
  counter; a stale handle resolves to `nullptr` instead of a dangling pointer, and recycled
  slots get a new generation. `O5MResourceManager` is the singleton owner with reference
  counting and a free list.
- **`O5MMeshResource`.** glTF/GLB loading through tinygltf into device-local vertex/index
  buffers, with position/color/normal/tangent/UV attributes and 16-bit index support.
- **`O5MTextureResource`.** stb_image decode → staging upload → device-local image and view.
- **`O5MShaderResource`.** GLSL compiled to SPIR-V at runtime with shaderc. There is no
  offline compile step; shaders ship as source and can be inspected while debugging.

### Scene and ECS (`core/Inc/Scene`)

- **`O5MWorld`** owns the entity slots: creation, generation-checked `O5MEntityHandle`s,
  deferred destruction that is safe to request from inside iteration, and centralized frame
  time (time scale, pause, delta-time clamping).
- **Components** are one per type with compile-time type IDs: `O5MTransformComponent`,
  `O5MCameraComponent` (the FPS camera, including the Vulkan Y-flip and 0..1 depth
  conventions), plus `O5MMeshComponent`, which is wired up during Phase 3.
- **Renderer (`core/Inc/Renderer`).** The frame-local draw snapshot `O5MDrawItem` /
  `O5MDrawList` produced by the collect stage, plus (from M4) `O5MCullingSystem`.
  It may include RHI types; `Scene` may not.

### Platform and events (`core/Inc/Platform`, `core/Inc/Events`)

- **`O5MWindow`** keeps GLFW opaque behind its header: window, surface creation, required
  instance extensions, resize flag, key level state and the event pump.
- **`O5MEventBus`** is a type-indexed bus with token-based subscriptions; unsubscribing
  during dispatch is deferred, so handlers can safely unsubscribe themselves.

---

## Architecture

The dependency direction is enforced by hand and never reversed:

```text
apps/
  o5m_viewer     main program (window, frame loop, graph wiring)
  rhi_verify     headless offscreen regression test  (permanent)
  tex_verify     TextureResource acceptance range
  mesh_verify    MeshResource acceptance range (hand-built GLB)

core/  (static library o5m_core, Inc/ and Src/ mirrored)
  Platform      O5MWindow            GLFW window, input, surface, event pump
  Events        O5MEvent*            event bus, keyboard/mouse events
  Scene         O5MWorld / O5MEntity ECS, transform, FPS camera
  Renderer      O5MDrawItem / O5MDrawList (per-frame draw snapshot), culling
  RenderGraph   O5MRendergraph       declarations -> barriers/lifetime -> execution
  RHI           O5MDevice / Swapchain / Pipeline
                                     the only place Vulkan API calls live
  Resources     O5MResource*         glTF meshes, textures, runtime-compiled shaders

      Platform -> Events -> Scene -> Renderer -> RenderGraph -> RHI   (never the other way)

third_party/  vendored stb
FetchContent  tinygltf v2.9.4
system        Vulkan SDK (vulkan + shaderc), GLFW3, glm
```

House rules that new code is expected to follow:

- Only `O5MDevice.cpp` may create buffers, images or allocate memory.
- Resources are strong handles, never raw pointers and never string lookups.
- Passes declare intent; synchronization, layouts and lifetime are derived, not written.
- Work stays inside its frame stage: asset handles resolve in the collect stage, `vkCmd*`
  is emitted only in the record stage (see [Frame pipeline](#frame-pipeline)).
- Class names carry the `O5M` prefix; the include root is `core/Inc`
  (`#include "RHI/O5MDevice.h"`).
- C++20, RAII for every Vulkan object, dynamic rendering only.
- Code comments are in English.

---

## Frame pipeline

Every frame walks the same seven-stage timeline. The stages are defined by *when*
information becomes available, and most synchronization bugs are work moved across one of
the boundaries:

```text
once, at startup
  [0] load        device, swapchain, shaders, pipelines, descriptors,
                  GLB -> MeshResource::load() (device-local VB/IB), scene assembly
                  the only stage allowed to create GPU objects

every frame
  [1] update      pollEvents -> world.update(dt) -> transform dirty -> camera
                  the only stage that mutates the scene
  [2] collect     forEachEntity -> handle.get() -> DrawItem -> DrawList,
                  plus one binding entry per unique vk::Buffer
                  the only render stage that resolves handles or reads the ECS
  [3] declare     import* -> markOutput -> addGraphicPass(setup, record)
                  setup() runs immediately and becomes the pass reads/writes
                  pure CPU: no vkCmd*, no resource contents touched
  [4] compile     cycle check -> topological order -> two-round state simulation
                  -> baked barrier plans; created resources are allocated here
                  pure CPU planning
  [5] record      execute(): cmd.begin()
                  per pass: enter barriers -> ctx -> record lambda -> exit barriers
                  cmd.end() -> queue.submit(...)
                  the only stage allowed to emit vkCmd*
  [6] execute     the GPU runs independently; ordering is semaphores and fences
  [7] present     present(renderFinished); the next frame waits on its fence
```

The graph touches three different callbacks, and they are not interchangeable:

| | collector — stage 2 | setup lambda — stage 3 | record lambda — stage 5 |
| --- | --- | --- | --- |
| runs | in the frame loop, after `update()` | inside `addGraphicPass()`, immediately | inside `execute()`, with the command buffer open |
| holds | the world, the resource manager | `O5MPassBuilder&` | `O5MRenderContext&`, `vk::raii::CommandBuffer&` |
| produces | `DrawList` + per-frame buffer bindings | `PassDesc` reads/writes | GPU commands |
| never | touches the graph | emits `vkCmd*` or reads resource contents | reads the scene, imports, builds pipelines |

Consequences worth writing down, because the compiler will not enforce them:

- **A handle is resolved exactly once per mesh per frame, in stage 2.** Everything
  downstream consumes plain `vk::Buffer` values; the recorder never sees an asset handle.
  That single resolution point is what keeps `Scene` free of Vulkan types.
- **A missing declaration is not an error — it is a missing barrier.** A wrong bind is not
  checked at all (only synchronization validation notices). Declarations and binds must
  therefore be derived from the same collected data.
- **Draw items are frame snapshots.** The buffers inside them must stay alive until that
  frame's fence signals, not merely until `execute()` returns: with frames in flight the
  GPU can still be reading frame N while the CPU already runs N+1.
- **The viewer rebuilds the graph every frame**, so stages 3 and 4 run per frame as well.
  They are pure CPU work and must stay cheap; nothing in them may touch the scene or the
  resource pool.

---

## Repository layout

```text
CMakeLists.txt          top-level build: deps, core, apps, tests
core/
  CMakeLists.txt        o5m_core static library (GLOB_RECURSE over Src/)
  Inc/ Src/             mirrored module tree: RHI, RenderGraph, Renderer,
                        Resources, Scene, Events, Platform
apps/
  o5m_viewer.cpp        Phase 2+ main program (window + real-time frame loop)
  rhi_verify.cpp        headless full-chain verification, exit code 0 == pass
  tex_verify.cpp        texture upload + sampling + sRGB round-trip probes
  mesh_verify.cpp       GLB loading checks (uint16 indices, attributes, offsets)
test/
  resmgr_cycle_test.cpp resource manager create/acquire/release cycles
  rendergraph_tests.cpp cycles, dead-pass culling, lifecycle
  ecs_tests.cpp         entity identity, slot recycling, deferred destroy, dt
shaders/
  common/               shared GLSL blocks (camera UBO, per-object data)
  fullscreen/           viewer vertex/fragment shaders
  geometry/             PBR-era geometry shaders (Phase 3 in progress)
assets/                 models/ textures/ hdri/ drop-in dirs (empty placeholders in git)
experiments/            Phase 5 research sandbox (single-frame algorithm skeleton)
third_party/stb         vendored image decoding
.cache/vulkan-tut/      local reference material, not part of the build
```

Include paths are rooted at `core/Inc`, so consumers write `#include "Scene/O5MWorld.h"`
regardless of where the file lives.

---

## Build

Requirements:

| Requirement | Notes |
| --- | --- |
| CMake ≥ 3.21 | `cmake --version` |
| C++20 compiler | AppleClang / GCC / Clang |
| [Vulkan SDK](https://vulkan.lunarg.com/) | must include **shaderc** and the RAII headers |
| GLFW 3 | `brew install glfw` · `apt install libglfw3-dev` · `vcpkg install glfw3` |
| glm | `brew install glm` · `apt install libglm-dev` · `vcpkg install glm` |
| tinygltf v2.9.4 | fetched automatically with `FetchContent` |
| stb | vendored in `third_party/` |

```bash
git clone https://github.com/Arkghem/-o-5M.git
cd -o-5M

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On macOS install the runtime dependencies first:

```bash
brew install glfw glm
# plus the LunarG Vulkan SDK (MoltenVK + shaderc)
```

`GLM_FORCE_DEPTH_ZERO_TO_ONE` is defined globally in the top-level `CMakeLists.txt` on
purpose: `core/` and `apps/` must agree on glm's depth convention or the two translation
units diverge silently (ODR).

Targets produced: `o5m_core`, `o5m_viewer`, `rhi_verify`, `tex_verify`, `mesh_verify`,
`resmgr_cycle_test`, `rendergraph_tests`, `ecs_tests`.

---

## Running

### `o5m_viewer` — the real-time program

```bash
./build/o5m_viewer                      # ESC or window close to quit
./build/o5m_viewer --frames 300         # unattended smoke run: exit after 300 frames
./build/o5m_viewer --rebuild-every 60   # force a swapchain rebuild every 60 frames
```

Controls: `W`/`A`/`S`/`D` to move, mouse to look around, scroll wheel to zoom, `ESC` to
quit. Camera key levels only record state — movement is integrated once per frame in
`O5MCameraComponent::update`.

Exit codes: `0` on a clean exit (window closed or `--frames` reached), `1` on an exception.

When the `VK_LAYER_KHRONOS_validation` layer is installed, the instance enables it together
with synchronization validation, so every run doubles as a sync check; the resize path in
particular is exercised with `--rebuild-every` because a real window drag cannot be
triggered unattended.

What one frame currently does (the full stage model, including the collect stage that
Phase 3 adds, is in [Frame pipeline](#frame-pipeline)):

1. `pollEvents()` dispatches window events; the app layer subscribes to the event bus
   (ESC request, camera key/mouse/scroll handlers).
2. `O5MWorld::update(dt)` integrates components with a single measured delta time.
3. The frame slot (`frameIndex % 2`) is synchronized: `waitForFences`, then
   `swapchain.acquire()`.
4. A small render graph is built for this frame only — the acquired swapchain image is
   imported with `importTexture(..., eUndefined)` and `TexWrite::Present`, and the
   per-slot camera UBO is imported as a uniform buffer.
5. `graph->execute(...)` records the baked barriers and the pass, submits with the frame's
   fence and the acquire/render-finished semaphores, then `present()` runs.

Synchronization is split into two pools on purpose, which is the heart of the Phase 2
exercises: command buffers, fences, `imageAvailable` semaphores and camera UBOs live
**per frame slot**, while `renderFinished` semaphores live **per swapchain image** (the
presentation engine still holds an image until it is re-acquired). Resize takes priority
over everything else in the loop, and fences are only reset once a submit is certain to
happen.

### Verification programs

These are headless (no display required) and are the regression gate for every change:

```bash
./build/rhi_verify            # exit code 0 == full render-graph chain passes
./build/tex_verify            # PNG -> upload -> sample -> sRGB round-trip probes
./build/mesh_verify           # GLB attributes, uint16 indices, primitive offsets
```

`rhi_verify` is the permanent one: it builds a graph out of a `render` pass (UBO-driven
gradient into an RGBA16F target), a `postprocess` pass (sample + Reinhard tonemap) and
`readback` passes, compiles it, submits two frames on one command buffer (exercising
`readHistory` ping-pong and the frame-0/steady-state barrier plans), waits on a fence, then
verifies the pixels on the CPU — both the UBO/tonemap math and the fact that frame 1
sampled frame 0's output. It also dumps the compiled graph (execution order, lifecycle,
baked barriers), which is the main way the graph is debugged.

Unit tests run through CTest:

```bash
ctest --test-dir build --output-on-failure
# resmgr_cycle, rendergraph (cycles / dead-pass / lifecycle), ecs
```

---

## Roadmap

| Phase | Scope | Status |
| --- | --- | --- |
| **P0** Foundation repair | `O5MResourceManager` handle pool, `TextureResource` upload path, `MeshResource` normals/tangents/16-bit indices | ✅ done |
| **P1** Render graph rewrite | Declaration/physical split, handle-based resource declarations (debug names interned to handles once), compute as a first-class pass kind, `readHistory` ping-pong with two-round barrier baking, imports, dead-pass culling, cycle detection, usage/lifetime inference | ✅ done |
| **P2** Window + real-time loop | GLFW window, surface, swapchain rebuild, event bus, `frames-in-flight = 2`, FPS camera, dynamic rendering with no per-frame `waitIdle` | ✅ done |
| **P3** Geometry pipeline | Vertex input bound to `MeshResource`, depth buffer and `writeDepth`, stencil exercise, MRT, `O5MDescriptorAllocator`, ECS integration (transform/mesh/draw items, frustum culling) | 🚧 in progress |
| **P4** PBR | Materials and textures → Cook-Torrance → shadow mapping with PCF → IBL (including compute mipmap generation) → integration and debug views (albedo / normal / roughness / shadow) | ⏳ planned |
| **P5** Research-ready layer | Shader hot reload, per-pass GPU timers, an experiments template, compute software ray tracing (macOS has no hardware RT) | ⏳ planned |

Phase 3 is being implemented step by step and everything else is downstream of it: until a
GLB mesh can be drawn with correct occlusion, materials, shadows and IBL have nothing to
shade.

---

## Design notes and non-goals

These are deliberate boundaries for the current stage, not oversights:

- **Single queue.** No async compute or transfer queue yet; the graph assumes one graphics
  queue.
- **One writer per resource.** WAW edges are not modeled, so a resource may not be written
  by two passes.
- **No memory aliasing.** Resource memory is not reused across lifetimes yet (a known
  stretch goal).
- **No VMA.** One `vkAllocateMemory` per resource, with the ~4096 allocation ceiling
  tracked explicitly rather than hidden.
- **No input attachments.** Subpass-style input attachment reads are not part of the
  model.
- **Names are debug-only.** Nothing resolves a resource by string at runtime; handles are
  the identity, and interned names exist purely for `dump()` and error messages.
- **The graph is rebuilt per frame in the viewer.** The swapchain image changes every
  frame, so building a one-frame graph is cheaper and simpler than tracking per-image
  state; `compile()` is pure CPU work.

---

## Third-party libraries

| Library | Used for | How it is pulled in |
| --- | --- | --- |
| [vulkan-hpp](https://github.com/KhronosGroup/Vulkan-Hpp) (RAII) | Vulkan C++ bindings | Vulkan SDK |
| [shaderc](https://github.com/google/shaderc) | GLSL → SPIR-V at runtime | Vulkan SDK |
| [tinygltf](https://github.com/syoyo/tinygltf) | glTF/GLB mesh loading | `FetchContent`, tag `v2.9.4` |
| [glm](https://github.com/g-truc/glm) | math (built with `GLM_FORCE_DEPTH_ZERO_TO_ONE`) | system package |
| [GLFW](https://www.glfw.org/) | window, input, surface | system package |
| [stb](https://github.com/nothings/stb) | image decoding (`stb_image`) | vendored in `third_party/stb` |

---

## Platform notes

Development and verification happen on macOS, where Vulkan runs on top of MoltenVK. Points
worth knowing when reading the code:

- **Portability subset.** The instance is created with
  `vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR` — without it MoltenVK enumerates no
  physical device at all — and `VK_KHR_portability_subset` is enabled on the device.
  Extension support is queried rather than assumed.
- **Depth formats.** Single-sample depth is planned around `D32_SFLOAT`, picked through a
  runtime format query and the most reliably supported choice on MoltenVK; the stencil
  exercise will use a combined depth/stencil format. `O5MDevice::aspectFromFormat()` already
  maps both to the right aspect mask.
- **Y flip and depth range.** Vulkan's clip space differs from OpenGL's: the Y flip is
  folded into the projection matrix in `O5MCameraComponent`, and the `[0, 1]` depth range
  comes from the global `GLM_FORCE_DEPTH_ZERO_TO_ONE` definition.
- **No hardware ray tracing.** MoltenVK exposes no RT extensions, so Phase 5 ray tracing is
  planned as a compute shader with a BVH in a storage buffer — which exercises the compute
  and history paths far better than a vendor-specific extension would.
- Linux builds are expected to work with the same CMake flow (GLFW + glm + Vulkan SDK);
  macOS is simply the configuration that is exercised continuously.

---

## License

[MIT](LICENSE) © MaYu
