## QA Run — 2026-08-06

### GL Version
- **Exit code**: 139 (SIGSEGV)
- **Output**: None (crash before any output)
- **Root cause**: Known macOS GL 4.1 + DSA incompatibility. macOS caps at OpenGL 4.1, but the codebase requires 4.6 Core with DSA — immediate crash.

### VK Version
- **Exit code**: 1
- **Failure point**: `[RHI_VERIFY] PSO create failed`
- **Root cause**: Depth-stencil format `VK_FORMAT_D24_UNORM_S8_UINT` is not supported on Apple M3 Max via MoltenVK. The MoltenVK layer falls back to `VK_FORMAT_D32_SFLOAT_S8_UINT` for the image, but the image view and render pass still reference the original unsupported `D24_UNORM_S8_UINT` format, causing the render pass attachment to fail format feature checks.
- **Validation warnings observed**:
  1. `vkCreateImage`: `VK_ERROR_FORMAT_NOT_SUPPORTED` for `VK_FORMAT_D24_UNORM_S8_UINT` → MoltenVK auto-fallback to `D32_SFLOAT_S8_UINT`
  2. `vkCreateImageView`: Format `D24_UNORM_S8_UINT` has no supported features on this device
  3. `vkCreateRenderPass`: Depth attachment format `D24_UNORM_S8_UINT` lacks `VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT`
- **No VUID- validation errors from the application side** — all format issues are MoltenVK compatibility warnings.

### Verdict
- GL: **CRASH** (known macOS limitation)
- VK: **FAIL** (exit 1, PSO create failed due to depth format)
- VK Validation: **ISSUES** (format incompatibility on Apple Silicon / MoltenVK)
- **Overall: FAIL** — VK does not pass QA.

### Suggested Fix
The VK RHI layer hardcodes `VK_FORMAT_D24_UNORM_S8_UINT` for depth-stencil. On Apple Silicon (MoltenVK), this format is unsupported. The fix requires:
1. Query supported depth formats at device init (`vkGetPhysicalDeviceFormatProperties`)
2. Select a supported format (e.g., `VK_FORMAT_D32_SFLOAT_S8_UINT` on MoltenVK)
3. Use the selected format throughout image creation, image view, and render pass
