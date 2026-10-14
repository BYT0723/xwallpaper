# 报告：scene 模块 vs linux-wallpaperengine scene 模块 —— 功能相似度评估

- 日期：2026-10-14
- 本侧：`src/scene*.c/.h`（分支 `refactor/scene-render-pipeline`，HEAD `b5a1eee`）
- 参考：`/home/walter/Workspace/Github/linux-wallpaperengine`（GPL-3.0，仅作行为对标，不取代码）
- 结论摘要：**总体功能相似度约 66%（区间 60–70%）**；渲染主链路（shader/material/effect/image）已达
  80–90%，差距集中在**高级粒子特性、脚本 API、文本排版、相机/合成顺序**四块。

---

## 1. 评估口径

不是比"代码长得像不像"，而是回答：

> 给同一份 Wallpaper Engine 场景数据（`scene.json` + `scene.pkg` + `project.json`），
> 两边**产出的画面与行为**覆盖到什么程度？

三条证据线：

1. **能力清单比对**：逐子系统列参考支持的特性，标注本侧状态（实现 / 近似 / 降级 / 缺失）。
2. **真实壁纸统计**：11 个 workshop scene pkg 的 JSON 全量统计（operator/initializer 实际用量）。
3. **同帧基准**：用参考自带截图作 ground truth 逐像素比对（`XW_FIXED_TIME` 确定性渲染下）。

覆盖度是**估算值**，每项后附证据与置信度。权重按"对典型 scene 壁纸画面的贡献"给。

---

## 2. 规模对比

| | 本侧 | 参考（scene 相关） |
|---|---|---|
| 场景模块代码 | `src/scene*.c/.h` = **9,518** 行 | `Render/` + `Data/Parsers` + `Scripting` + `Render/Wallpapers` 相关 ≈ **12–15k** 行（不含 drivers/media/audio driver） |
| 最大单模块 | `scene_gl.c` 2,308 | `CParticle.cpp` **2,323** |
| 第二/第三 | `scene_parse.c` 1,308 / `scene_shader.c` 1,153 | `CImage.cpp` 1,148 / `CPass.cpp` 1,114 |

规模同量级。**双方最大的单文件都是粒子系统**，这也是差距最大的地方。

---

## 3. 子系统逐项矩阵

图例：`✅` 行为对齐 · `🟡` 近似/保守降级 · `🔴` 未实现

### 3.1 shader / material 管线 —— 覆盖 **≈90%**

| 能力 | 参考 | 本侧 |
|---|---|---|
| WE GLSL 方言预处理（`mul/lerp/saturate/CASTn/texSample2D`…） | `ShaderUnit` + `GLSLContext` | ✅ `scene_shader.c` |
| glslang → SPIR-V → SPIRV-Cross 回译 | 自建 fork | ✅ 系统库（`scene_glsl.c`） |
| `// [COMBO]` → `#define`、material 常量覆盖 | ✅ | ✅ |
| 采样器 `// {"material":…}` 注释收集 | ✅ | ✅ |
| 采样器 `// {"default":"util/perlin_256"}` 默认纹理 | ✅ | ✅（`<assets>/materials/util/*.tex`） |
| 采样器 combo（仅实际绑定该槽时定义） | ✅ | ✅ |
| 链接 varying 宽窄不一致（`glitter_prepare` 型） | 由其 fork 处理 | ✅ `xw_shader_fix_linked_varyings` |
| 内建头 `common*.h` | 自带 | ✅ 按官方文档重写（已与真 assets 核对一致） |
| `ApplyBlending` 数值枚举 | 编译期 `#if BLENDMODE==N` 链 | ✅ 按真资源编号（含 32 = `mix(A, A+A*B, opacity)`） |
| puppet/几何着色器（3D 模型顶点路径） | ✅ | 🔴 |

置信度：高（有真 assets 逐项核对记录）。

### 3.2 image / effect 运行时 —— 覆盖 **≈78%**

| 能力 | 参考 | 本侧 |
|---|---|---|
| model/material 解析 | ✅ | ✅ |
| effect 链：passes / fbos / combos / constantshadervalues | ✅ | ✅ |
| per-pass `target` 离屏 + `bind: previous|_rt_*` | ✅ `CPass` | ✅（M3，命名 target 表 + 序列语义） |
| per-effect `visible` | ✅ | ✅ |
| 混合模式 / `colorBlendMode` | ✅ | ✅ |
| `_rt_FullFrameBuffer`（每帧一份） | ✅ | ✅（本轮修：原为每对象一份，会叠加） |
| 跨对象 `_rt_imageLayerComposite_<id>_a/b` | ✅ 每个图像对象注册一对 | 🔴 **保守降级**（无法解析的 `_rt_*` → 保留底图） |
| `alignment`（top/bottom/left/right） | ✅ | 🔴 |
| `fullscreen` 图层 | ✅ | 🔴 |
| `passthrough` / `solidlayer` | ✅ | 🔴 |
| `animationlayers`（图像序列动画层） | ✅ | 🔴 |
| puppet 网格（3D 模型图） | ✅ | 🔴 |
| 逐对象 parallax 深度 | ✅ | 🔴 |

置信度：中高。跨对象合成那项已实测（修前天空有一层"裂纹"垃圾，修后与参考一致）。

### 3.3 合成与渲染顺序 —— 覆盖 **≈70%**

| 能力 | 参考 | 本侧 |
|---|---|---|
| 对象 FBO ping-pong 链 | ✅ | ✅ |
| effect 内命名 target | ✅ | ✅ |
| `dependencies` 驱动的对象渲染顺序 | ✅ `addObjectToRenderOrder` | ✅ 同款 DFS 后序（2026-10-14） |
| 场景空间合成目标 | ✅ | 🔴 |
| bloom（`_rt_Bloom` + 内建 effect 图） | ✅ | 🔴 只解析了 `bloom` 标志，未渲染 |
| fullscreen 图层参与投影尺寸 | ✅ | 🔴 |

置信度：中高（渲染顺序在 `scene_image.c` / `scene.c` 直接可见为 per-type 循环）。

### 3.4 粒子系统 —— 覆盖 **≈62%**（差距最大）

| 能力 | 参考 | 本侧 |
|---|---|---|
| emitter `boxrandom` / `sphererandom` | ✅ | ✅（盒 + 圆盘/球壳） |
| emitter `sphererandom` | ✅ | 🔴 |
| `rate` / `origin` / `directions` | ✅ | ✅ |
| `instantaneous` / `duration` / `delay` / 周期发射 / `limitOnePerFrame` | ✅ | 🔴 |
| initializer 数量 | **9** | **7** |
| ↳ `velocityrandom` `sizerandom` `alpharandom` `colorrandom` `rotationrandom` `lifetimerandom` `turbulentvelocityrandom` | ✅ | ✅ |
| ↳ `angularvelocityrandom` `mapsequencearoundcontrolpoint` | ✅ | 🔴 |
| operator 数量 | **12** | **5** |
| ↳ `movement`（gravity/drag）`alphafade` `oscillateposition` `oscillatealpha` `oscillatesize` | ✅ | ✅ |
| ↳ `angularmovement` `sizechange` `alphachange` `colorchange` `turbulence` `vortex` `controlpointattract` | ✅ | 🔴 |
| renderer `sprite` / `spritetrail` | ✅ | ✅ |
| renderer `rope` / `ropetrail` | ✅ | 🔴 |
| 材质 pass（17 float/顶点：`a_Position/a_TexCoordVec4/a_Color/…`） | ✅ | ✅ `genericparticle` 可编译运行 |
| 图集逐帧采样（TEXS 时长、`sequence`/`randomframe`） | ✅ | ✅ |
| 正交投影 / `flags&4` 透视投影 | ✅ | ✅ |
| 控制点（含 `linkMouse`） | ✅ | 🔴 |
| 子粒子系统 / children | ✅ | 🔴 |
| 音频驱动发射（`audioProcessing*`） | 🟡 参考亦仅部分 | 🔴 |
| `fadeAlpha` / `fadeSize`（rope 用） | ✅ | 🔴 |

置信度：高（清单来自 `setupInitializers` / `setupOperators` 的 `is<…>` 分支，逐项可比）。
**缓解事实**：11 个真实 pkg 的统计显示，实际用到的 operator/initializer 很少
（`rotationrandom`×3、`alpharandom`×2、`randomframe`×2、`colorrandom`×1、`turbulentvelocityrandom`×1；
oscillators / ropetrail / children / controlpoint offset 全为 0）——所以**多数壁纸不受影响**。

### 3.5 文本 —— 覆盖 **≈55%**

| 能力 | 参考 | 本侧 |
|---|---|---|
| font / size / color / content | ✅ | ✅（FreeType 光栅化；**格式曾不匹配真实数据，2026-10-14 已修**） |
| **script layer**（JS 驱动文本内容） | ✅ `CText::initScriptLayer` | ✅ `update(value)` 每帧驱动（2026-10-14） |
| UTF-8 / CJK shaping | ✅ | 🔴（Latin-1 单行） |
| 对齐 / 锚点 / 换行 / maxwidth | ✅ | 🔴 |
| 富文本标记 | ✅ | 🔴 |

### 3.6 音频 —— 覆盖 **≈60%**

| 能力 | 参考 | 本侧 |
|---|---|---|
| 加载 / 循环（`playbackmode`）/ 音量 / mute | ✅ | ✅（SDL_mixer） |
| 每对象多音源 | ✅ | ✅ |
| **频谱分析**（喂音频反应 uniform） | ✅ `AudioStream` | 🔴（恒 0） |
| 媒体集成（DBus media source）/ 录音驱动 | ✅ | 🔴 |

### 3.7 脚本（JS）—— 覆盖 **≈32%**

| 能力 | 参考 | 本侧 |
|---|---|---|
| quickjs 运行时 | ✅ | ✅（vendored） |
| 执行场景脚本 | ✅ | ✅ |
| 全局对象 | `engine` / `thisScene` / `input` / `console` / `shared` | 🟡 仅 `scene` + `input` |
| `engine`（frametime / runtime / daytime / openUserShortcut / media） | ✅ | 🔴 |
| `thisScene` 相机/bloom/clear 读写 | ✅ | 🟡 仅 object 级 alpha/origin/scale/visible/property |
| 模块系统（`import` + `weColor`/`weMath`） | ✅ | 🔴 |
| `VectorAdapter`（vec2/3/4 带方法） | ✅ | 🔴 |
| 定时器 | `engine.setTimeout/…` | ✅ 全局 `setTimeout/setInterval` |
| 文本层脚本 | ✅ | 🔴 |
| 脚本属性**求值**（`{"script":…}` 每帧驱动） | ✅ | 🟡 只取基准 `value`，不求值 |
| 用户属性（`--list-properties` / `--set-property`） | 参考用 CLI 参数 | ✅ 本侧有等价能力 |

### 3.8 相机 —— 覆盖 **≈47%**

| 能力 | 参考 | 本侧 |
|---|---|---|
| 正交投影（`orthogonalprojection`） | ✅ | ✅ |
| 透视（fov/nearz/farz，粒子 `flags&4`） | ✅ | ✅ |
| `clearcolor` | ✅ | ✅ |
| `eye` / `center` lookAt（相机平移） | ✅ | 🟡 **实为无操作**（语料 `center=eye+(0,0,-1)`，与投影的 `translate(eye)` 抵消） |
| `cameraparallax` | ✅ | 🔴（只解析标志） |
| 自动投影尺寸（`isAuto`） | ✅ | ✅ `xw_camera_auto_size`（**报告此前误判为缺失**） |
| 脚本可写相机 | ✅ | 🔴 |
| bloom 参数 | ✅ | 🟡 解析未渲染 |

### 3.9 打包 / 纹理解码 —— 覆盖 **≈85%**

| 能力 | 参考 | 本侧 |
|---|---|---|
| `scene.pkg`（PKGV）解析 | ✅ `PackageParser` | ✅ `scene_pkg.c` |
| TEXV 解码（含内嵌 PNG） | ✅ | ✅ |
| TEXB0003 / TEXB0004 | ✅ | ✅（视频纹理拒绝，另有 video 后端） |
| TEXS 动画块（帧矩形 + 逐帧时长） | ✅ | ✅ |
| 虚拟文件系统 / asset locator | ✅ `Container` | ✅（含 Steam 库自动探测） |
| 视频纹理（MPV） | ✅ | 🟡 由独立 video 后端承担 |

---

## 4. 量化结论

按"对典型 scene 壁纸画面的贡献"加权：

| 子系统 | 覆盖度 | 权重 | 贡献 |
|---|---|---|---|
| shader / material 管线 | 90% | 20% | 18.0 |
| image / effect 运行时 | 78% | 20% | 15.6 |
| 粒子系统 | 62% | 20% | 12.4 |
| 合成与渲染顺序 | 70% | 12% | 8.4 |
| 脚本 | 32% | 8% | 2.6 |
| 文本 | 55% | 6% | 3.3 |
| 相机 | 55% | 5% | 2.75 |
| 音频 | 60% | 5% | 3.0 |
| 打包 / 解码 | 85% | 4% | 3.4 |
| **合计** | | 100% | **≈69.5%** |

> **总体功能相似度 ≈ 69%（区间 60–70%）**

交叉验证（不是同一个数，但方向一致）：

- 逐像素基准：2178468632 整帧 RMSE **0.0486 → 0.0225**（约 2.3%/通道）；**关粒子后基础层与参考几乎
  完全一致**（均值 0.2023 vs 0.2024，标准差 0.1636 vs 0.1640）。
- 真实壁纸可运行率：手头 11 个 pkg 抽样，绝大多数能出图；退化集中在 rope/控制点/文本层/3D 模型类。

---

## 5. 差距分类

- **A 类｜行为对齐（照参考流程重写）**：shader 方言与转译、effect pass/target/bind、混合模式、
  粒子材质 pass、图集逐帧、正交/透视投影、TEXV/TEXS 解码、assets 探测。→ 这些是"相似度"的主要来源。
- **B 类｜保守降级（有替代路径，观感可接受）**：跨对象 `_rt_imageLayerComposite_*`、不可解析的
  `_rt_*`/shader 失败时保留底图、视频纹理交给独立后端。→ 不崩、不画垃圾，但不等于参考。
- **C 类｜未实现（静默忽略）**：12 个 operator 中的 7 个、`rope/ropetrail`、控制点、子粒子、
  振荡器、`fullscreen`/`alignment`/`passthrough`/`solidlayer`、`animationlayers`、puppet 网格、
  bloom 渲染、依赖排序、频谱音频、脚本模块/`engine`/`thisScene` 全量、文本脚本层、相机 eye/center。
- **D 类｜近似实现（参数或算法非逐位一致）**：
  - `turbulentvelocityrandom` 的噪声场（引擎未公开，用标准 Perlin + curl，置换表自生成）；
  - `ApplyBlending` 的数值枚举（已按真资源校正，但仍是推断）；
  - 内建头里 `DecompressNormal*`/`ConvertTexture0Format`/`BuildTangentSpace` 等官方只公布签名的函数；
  - 逐粒子贴图选择 / REFRACT 帧缓冲贡献 / alpha 对齐（当前残留的雨观感差异）。

---

## 5.5 2026-10-14 更新（数据驱动的重排 + 两项修正）

- **重排依据**：对 9 份真实壁纸做**全量 JSON 统计**（emitter / initializer / operator / renderer / 脚本 API 用量），
  而不是按模块代码量推断。据此把 `_rt_imageLayerComposite_*` 降为低优先，把振荡器与粒子缺省提前。
- **修正 1**：`_rt_imageLayerComposite_*` 对当前壁纸**零收益** —— 参考的 `CImage::render()` 在
  `visible==false` 时直接 `return`，且 `CFBO` 创建时清成**透明黑**（"Layer framebuffers must start transparent"），
  所以"空目标"参与混合近似无操作；**我们的保守降级行为等价**。
- **修正 2**：`directions` 缺省 `(1,1,1)` **早已实现**（`scene_parse.c`）—— 我在数据调查中判其为"缺失"是**误报**。
- **本轮落地**：`sphererandom`（12/22 emitter）、三个振荡器 operator（14 实例）、
  粒子配置缺省（`rate` 10 / `maxcount` 100，影响 8+3 处，此前**整层不可见**）。
- **新增缺口记录**：球发射器的 `speedmin/speedmax`（向外初速度，语料 1 处）；
  `angularmovement`(4) / `angularvelocityrandom`(4) / `controlpointattract`(4) 仍缺（下一轮候选）。
- **方法论修正（重要）**：3.5 文本一行原先是**按代码意图**而非**真实数据**核对的 —— 本轮用真包验证时发现，
  真实 text 对象的格式（内容在 `text` 本身 + 同级 `font`/`pointsize`/`color`）**此前根本不被解析**，
  2887099508 因此**整包加载失败**。已按真实格式修复。**教训：能力矩阵的每一行都必须用真包核对**，
  不能只看代码"看起来支持"。
- **又一次前提证伪（2026-10-14）**：计划中的"相机 eye/lookAt"经验证是**净无操作**（语料恒为
  `center=eye+(0,0,-1)`，与投影的 `translate(eye)` 抵消）→ 不做；同时更正报告里"auto 投影尺寸"的误判（其实已实现）。
- **2026-10-14 第五批落地**：**文本脚本层**（`update(value)` 每帧驱动文本，真机已验证时钟/日期 ✓）；
  同时记录**新缺口**：文本定位未走画布→窗口映射 ✗（落在左上角）。
- **2026-10-14 第四批落地**：**依赖序渲染**（`dependencies` 的 DFS 后序，取代按类型三趟）——
  对已测壁纸逐位无变化 ✓（场景自然顺序本就一致），但移除了结构性偏差。
- **2026-10-14 第三批落地**：拖尾 `g_RenderVar1.w` 改为引擎的**每帧**宽高比（雨丝由"短划线"恢复为"细长竖丝"）。
  整帧 RMSE 0.0225→0.0271 变差，原因是**斑点仍是数值主导项**（下一个目标）。
- **2026-10-14 第二批落地**：`angularmovement` + `angularvelocityrandom`（角运动）；
  真实 text 格式；animated/scripted 数值兜底（`{"animation":…,"value":…}`）。
  2887099508 从"整包拒绝"变为正常出图。

## 6. 结论与风险

**结论**：本侧 scene 模块是参考实现的一个**"渲染主链路完整、外围特性按需裁剪"**的等价物。
相似度不是均匀的——**决定画面正确性的那一段（shader→material→effect→image→合成）已经做到 80–90%**，
而差距堆在**长尾特性**（高级粒子算子、脚本 API 表面、文本排版、相机/排序）。

**风险排序**：

1. **脚本 API 表面**（32%）——最容易被"看起来能跑但属性不动"坑到；脚本属性只取基准值不求值，
   是最可能的用户可见故障点。
2. **粒子算子长尾**（2/12）——对用 rope/控制点/振荡器的壁纸会明显降级（好在真实语料用量低）。
3. **渲染顺序按类型而非依赖**——同一对象同时含 image+particle 依赖时层级会错。
4. **文本**（38%）——非拉丁文本、脚本文本直接不可用。

**下一步优先级建议**（按"相似度提升/工作量"）：
`_rt_imageLayerComposite_*` 真正产出 → 依赖排序 → 脚本 `thisScene`/`engine` 表面与脚本属性求值 →
bloom 渲染 → 剩余 operator → 文本脚本层。
