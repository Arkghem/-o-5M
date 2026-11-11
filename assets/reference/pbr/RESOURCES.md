# PBR 参考资源清单（Phase 4）

> 采集目的：为 M2（Cook-Torrance 直射）/ M3（阴影）/ M4（IBL）提供**对照图**与**少量测试资产**。
> 采集原则：优先 CC0；带署名要求者逐项记录作者与许可；不得绕过任何付费/登录限制。
> 采集日期：2026-11-08。所有下载均来自公开直链，无登录、无 ToS 冲突。

## 1. 本目录文件（reference 图，仅用于目视对照）

| 文件 | 内容 | 来源 | 许可 | 使用/再分发 |
|---|---|---|---|---|
| `damagedHelmet__khronos-sampleviewer.png` | DamagedHelmet 的参考渲染（130×126，小图） | [Khronos glTF-Sample-Assets / DamagedHelmet](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DamagedHelmet) | 原始模型 theblueturtle_ **CC BY-NC 4.0**；转 glTF 版 ctxwing **CC BY 4.0** → 整包按 **NC** 从严理解 | **仅本地学习/仅参考**；不得再分发、不得商用；署名：theblueturtle_、ctxwing |
| `metalRoughSpheres__khronos-sampleviewer.png` | 带纹理的 metal/roughness 球阵（338×329） | [MetalRoughSpheres](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/MetalRoughSpheres) | **CC BY 4.0**（© 2017 Analytical Graphics, Inc. / Ed Mackey） | 仅参考；再分发需署名 |
| `metalRoughSpheresNoTextures__khronos-sampleviewer.png` | 无纹理纯因子 metal/roughness 球阵（256×256）— **M2 主对照图** | [MetalRoughSpheresNoTextures](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/MetalRoughSpheresNoTextures) | **CC0 1.0**（© 2019 Krill Gavrilov / Open CASCADE） | 可自由使用、可再分发 |
| `hdri_studio_small_09__polyhaven-tonemapped.png` | Studio Small 09 的环境 equirect（tonemap 后预览） | [Poly Haven](https://polyhaven.com/a/studio_small_09) | **CC0** | 可自由使用 |
| `hdri_kloofendal_48d__polyhaven-tonemapped.png` | Kloofendal 48d Partly Cloudy（正午晴天 + 云） | [Poly Haven](https://polyhaven.com/a/kloofendal_48d_partly_cloudy) | **CC0** | 可自由使用 |
| `acg_Metal032_preview.png` 等 4 张 | ambientCG 材质的官方预览渲染（1024²，立方体 + 球） | ambientCG（`https://ambientcg.com/a/<ID>`） | **CC0**（含预览渲染图） | 可自由使用 |

备注：ambientCG 的预览图是「材质 + 环境 + 他们的 PBR」的合成结果，用来判断
albedo/roughness 语义是否符合预期，不能当作本引擎的 1:1 对照（环境光不同）。

## 2. 测试资产（已下载到 `assets/`）

| 文件 | 大小 | 用途 | 许可 |
|---|---|---|---|
| `assets/models/MetalRoughSpheresNoTextures.glb` | 291 KB | M1c/M2 靶场：25 球，金属/非金属 × 光滑/粗糙，仅用 factor，不依赖纹理与 IBL | **CC0** |
| `assets/hdri/studio_small_09_1k.hdr` | 1.6 MB | 校验 equirect→cube、irradiance/prefilter 数学（对比程序化天空） | **CC0** |
| `assets/hdri/kloofendal_48d_partly_cloudy_1k.hdr` | 1.6 MB | 同上，含高动态范围太阳，检验 tonemap/曝光 | **CC0** |
| `assets/models/DamagedHelmet.glb`（原有） | 3.7 MB | 纹理/法线/ORM 全链路 | CC BY-NC（见上） |

> 决策 #5 仍是「环境贴图程序化生成」。这两个 1k HDR **不进入运行时资产管线**，
> 只作为离线对照：程序化天空的亮度/色温是否合理、cube 转换是否有接缝/上下翻转。

## 3. 只给链接、未下载的资源（按需自取）

| 资源 | 类型 | 许可 | 备注 |
|---|---|---|---|
| [Poly Haven HDRIs](https://polyhaven.com/hdris)（如 `venice_sunset`、`spruit_sunrise`、`brown_photostudio_02`） | HDRI | CC0 | 直链格式 `https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k/<id>_1k.hdr` |
| [Poly Haven Textures](https://polyhaven.com/textures)（`metal_plate`、`rusty_metal_02`、`marble_01`、`leather_red_02`、`wood_floor_deck`） | PBR 贴图 | CC0 | 提供 **gltf 打包格式**（含 ARM=AO/Rough/Metal 合图），最贴合 glTF metallic-roughness |
| [ambientCG](https://ambientcg.com)（`Metal032`、`Metal049A`、`Plastic013A`、`Marble012`、`Concrete034`、`PavingStones138`） | PBR 贴图 | CC0 | 直链 `https://ambientcg.com/get?file=<ID>_1K-JPG.zip`（≈3–10 MB）；分通道贴图，接 glTF 需自己打包 ORM |
| [glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets) `MetalRoughSpheres.glb` | 模型 | CC BY 4.0 | 带纹理版，11 MB，纹理 2K |
| [Khronos glTF Sample Viewer（在线）](https://github.khronos.org/glTF-Sample-Viewer-Release/) | 参照渲染器 | — | 可加载任意 glb URL 现场对照，比静态截图更适合 A/B |
| [Khronos PBR Neutral Tonemapper](https://github.com/KhronosGroup/ToneMapping) | 参考实现 | Apache-2.0 | 若 ACES 过饱和，可换 PBR Neutral（有规范说明） |
| [Google Filament · Materials guide](https://google.github.io/filament/Materials.html) | 文档 | — | 与 glTF 材质语义 1:1 对应的工程化描述 |
| [LearnOpenGL · PBR Theory](https://learnopengl.com/PBR/Theory) / [Lighting](https://learnopengl.com/PBR/Lighting) / [IBL](https://learnopengl.com/PBR/IBL/Diffuse-irradiance) | 教程 | 站点文字 CC BY-NC 4.0，图片版权归站点 | **仅阅读/仅参考**，图不可再分发 |

## 4. 合规红线（给自己留痕）

- 本目录只放「对照用」图片，不参与构建、不进运行时资源表。
- DamagedHelmet 相关素材带 **NC**，任何「作品集公开 / 商用 / 再分发」场合都要替换成
  CC0 资产（`MetalRoughSpheresNoTextures` 或 Poly Haven 模型）。
- 若把 reference 图放进仓库，保留本文件（署名 + 许可）等同于许可声明；删除图片时
  一并删除对应行。
