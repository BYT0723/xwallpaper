# REA 逆向取证 + 本分支实现度对比

- 日期：2026-10-15
- 本侧：分支 `refactor/scene-render-pipeline`（`src/scene*.c/.h`，11,136 行）
- 参考：`/home/walter/Workspace/Github/linux-wallpaperengine` @ `b016d7d`（GPL-3.0，与已安装包 `r627.b016d7d1` 同源；仅作行为对标，不取代码）
- 本文与 `SIMILARITY.md` 的关系：口径不同。`SIMILARITY.md` 按"参考实现了什么"打分；
  本文按**真实语料的实际用量**打分，并且所有二进制侧结论都带 REA 证据 ID 或阻塞证据。
- 结论摘要：**语料加权的实现度约 65–70%**（与旧报告的 66% 落在同一区间，但差距的**位置**大不相同 —— 见 §6）。

---

## 1. 方法与证据边界（先说不可靠的地方）

### 1.1 REA 三条路线被实测证伪

| 路线 | 结果 | 原始证据 |
| --- | --- | --- |
| 原生反编译（`open_binary` → `analyze_function` / `procedure_pseudo_code`） | ❌ | `binary_session` 报 `ghidra: not_configured`（`GHIDRA_INSTALL_DIR` 未设）、`hopper: executable_missing`（`/opt/hopper/bin/Hopper`）、`ida: not_configured` |
| `inspect_binary_layout`（离线 ELF 布局/符号/重定位） | ❌ | `provider_unavailable`：`Set REA_PWNTOOLS_PYTHON to an absolute caller-supplied Python executable with pwntools 4.15.0, pyelftools 0.33 and Unicorn 2.1.2` |
| `inspect_artifact` 打安装目录 / 打 `scene.pkg` | ❌ | 目录：`target_unavailable … directory_requires_file`；`scene.pkg`：`unsupported binary format`（PKGV 私有容器，非 zip） |

因此**本报告没有任何"反编译得到的代码级证据"**。凡是涉及参考实现内部结构的结论，来源都是
其 GPL 源码 + 运行时观测，而不是二进制逆向。

### 1.2 REA 走通的一条路线：运行时过程取证

`capture_process_scenario` 可用（provider `rea-process` v3）。在 Xvfb 中隔离运行参考实现，
不对当前桌面产生副作用：

- `ev_d4b00494ca9d24fc596527bc1625a8cd2bee990c90e06321a5798679fd113eff`
  —— `linux-wallpaperengine --list-properties`（场景 `2889253135`）
- `ev_65d3bcc8e760cdaf390596c45ad512532f9974539d29d772d05c079d90b46b18`
  —— `linux-wallpaperengine --dump-structure`（场景 `2889253135`）

这两条是本文**唯一**的二进制/运行时侧一手证据。过程捕获的证据不支持 `inspect_analysis_view`
（`Parent Evidence operation "capture_process_scenario" does not support analysis views`），
所以正文直接引用其文本。

### 1.3 语料解包是自查，不是 REA

`scene.pkg` 是私有 `PKGV` 容器（magic `PKGV00xx`，版本分布 `0008/0018/0021/0022/0023/0024`），
REA 拒绝解析。本报告用仓库已有的解包逻辑（`src/scene_pkg.c:35`）的结构自行解包 8 个场景；
自检：8/8 `scene.json` 解析成功、全部条目的 `offset+size` 在界内（`oob=0`）。

### 1.4 REA skill 自身的指引

`reverse-engineer-anything` 明确写着：*"For ordinary analysis of a complete source repository,
use normal repository tools and do not run REA readiness or provider commands."*
参考实现是完整 GPL 源码，因此**实现面对比以源码为准**是符合 skill 指引的，不是绕过。

---

## 2. 目标身份（三方对齐）

| | 路径 | 身份 |
| --- | --- | --- |
| 参考源码 | `~/Workspace/Github/linux-wallpaperengine` | `b016d7d`，253 个源文件，scene 相关约 13.3k 行 |
| 已安装包 | pacman `linux-wallpaperengine-git r627.b016d7d1-1` | 与源码同 commit |
| 已安装二进制 | `/usr/bin/linux-wallpaperengine`（150 B wrapper）→ `/opt/linux-wallpaperengine/linux-wallpaperengine`（31 KB launcher）→ `liblinux-wallpaperengine-lib.so`（10.3 MB） | **stripped**，C++ 符号隐藏可见性；静态链 QuickJS（dynsym 有 4992 个 `JS_*`） |

二进制是 stripped + hidden visibility，即使补上 Ghidra，可恢复的特征映射也远少于直接读源码 ——
这是本次选择"源码为准"的技术理由，不只是省事。

---

## 3. 语料基准（真实用量，本文的新增部分）

8 个真实 workshop scene（431960），合计 **341 个 object**：

| 资产 | 数量 | 备注 |
| --- | --- | --- |
| `.tex` | 229 | **全部** `TEXV0005` |
| `.mdl` | 19 | **全部** 是 puppet 模型：`MDLV0016`×6、`MDLV0021`×2、`MDLV0023`×11 |
| `.json`（粒子） | 20 | |
| `effect.json` | 59 | 组分布 animate 28 / localeffects 11 / colorize 8 / blur 5 / distort 4 / enhance 3 |
| material pass | 188 | blending：translucent 90 / normal 85 / additive 13 |
| 文本 object | 30（6 个场景） | |
| `.js` 脚本 | **0** | 语料里没有任何脚本文件 |

**粒子面的真实用量（这是 S3 的判据）**

- emitter：`sphererandom` 11、`boxrandom` 9
- initializer：`lifetimerandom` 20、`sizerandom` 20、`velocityrandom` 16、`colorrandom` 15、
  `rotationrandom` 10、`angularvelocityrandom` 4、`turbulentvelocityrandom` 2、`alpharandom` 2
- operator：`movement` 18、`alphafade` 18、`oscillateposition` 5、`angularmovement` 4、
  `oscillatealpha` 3、`controlpointattract` 2
- renderer：`sprite` 16、`spritetrail` 3
- `animationmode`：`sequence` 7 / 无 12 / `randomframe` 1

**对象字段的真实用量（这是 S2 的判据）**（出现次数 / 涉及场景数）

`id` 341/8、`name` 341/8、`origin` 309/8、`size` 274/8、`image` 248/8、`angles` 251/7、
`scale` 219/8、`disablepropagation` 205/3、`parent` 183/4、`effects` 174/7、`visible` 168/8、
`parallaxDepth` 122/5、`locktransforms` 119/3、`color` 114/6、`solid` 112/5、`alpha` 99/4、
`colorBlendMode` 95/4、`perspective` 87/4、`copybackground` 86/3、`ledsource` 86/3、
`alignment` 64/3、`attachment` 59/1、`animationlayers` 45/3、`instanceoverride` 23/6、
`anchor` 26/4、`dependencies` 8/2、`clampuvs` 8/1

`general` 段里 8/8 场景都出现：`bloom`/`bloomstrength`/`bloomthreshold`、
`cameraparallax`(amount/delay/mouseinfluence)、`camerashake`(amplitude/roughness/speed)、
`clearcolor`、`ambientcolor`、`skylightcolor`、`orthogonalprojection`、`zoom`、`fov/nearz/farz`；
7/8 出现 `hdr`；4/8 出现 `wind*`、`gravity*`。

**instanceoverride 绑定来源**：`user` 21、`script` 1 —— 脚本驱动在语料里几乎不出现。

---

## 4. 运行时证据（参考实现，一手）

`--list-properties` 观测到的用户属性类型与实例：

```
bokeheffect - boolean        bokehparticles - slider (Min 0 / Max 1 / Step 0.1)
clock - boolean              dustmotes - boolean
lensflare - boolean          logo - combo (0=None, 1=EN, 2=JP)
schemecolor - color (4 float)  snow - slider
```

`--dump-structure` 观测到的解析结果（场景 `2889253135`）：

- `Objects count: 24`，24 个 `Object <id> <name>`，12 个 `Model models/…`，33 个 `Material`，
  **75 个 `Pass`**，33 次 `Blend mode` / `Culling mode` / `Textures count`
- Model 标志位被打印：`Autosize` / `Passthrough` / `Fullscreen` / `No padding` / `Solid layer`
- Pass 细节：`Depth test` / `Depth write` / `Blend mode` / `Culling mode` / `Textures count` / `Combos count`（如 `VERSION: 2`）
- 着色器模块解析：9 次 `Resolving require module: LightingV1 in shader genericparticle`
- `materials/util/fullscreenlayer.json` + 纹理 `_rt_FullFrameBuffer` → 后处理层走全屏四边形
- Image effect：`Image effect 47` / `Default visibility status: 1` / `Dependencies count: 4`
- **`Unsupported puppet model header MDLV0016 in models/Rin_puppet.mdl`**

最后一条是**只有跑起来才看得到**的结论，见 §6。

---

## 5. 逐子系统对比矩阵

图例：参考列为参考实现状态；本侧列为 `refactor/scene-render-pipeline` 状态；
"语料"列是该子系统在 8 个真实场景中的实际压力。

### S1 shader / material 管线 —— 语料压力：高

| 能力 | 参考 | 本侧 |
| --- | --- | --- |
| WE GLSL 方言预处理（`mul/lerp/saturate/CASTn/texSample2D`…） | ✅ `ShaderUnit.cpp:22` | ✅ `scene_shader.c:342` |
| glslang→SPIR-V→SPIRV-Cross 回译 | ✅ `GLSLContext.cpp:134` | ✅ `scene_glsl.c:141` |
| `#include`（含 assets 覆盖与内建 `common*.h`） | ✅ `ShaderUnit.cpp:136` | ✅ `scene_shader.c:559` |
| `// [COMBO]` → `#define`，material 值优先 | ✅ `ShaderUnit.cpp:442` | ✅ `scene_shader.c:380` |
| `#require` 模块（`LightingV1`） | ✅ 桩函数返回 `vec3(0)` `ShaderUnit.cpp:357` | 语料 9 次触发；本侧按 assets 内建头处理 |
| 采样器 `default` / `util/*` 内建纹理 | ✅ `AssetLocator.cpp:72` | ✅ `scene_image.c:100` |
| 内建 uniform（`g_Time/g_Daytime/g_Alpha/g_Color/g_ModelViewProjection*`…） | ✅ `CPass.cpp:856` | ✅（子集）`scene_gl.c:1823` |
| `ApplyBlending` 数值枚举（含 32） | ✅ `MaterialParser.cpp:73` | ✅ `scene_shader.c:97` |
| 纹理 `g_Texture0..7` / `g_TextureNResolution` | ✅ `CPass.cpp:824` | 部分 |
| `usertextures` | ✅ `MaterialParser.cpp:39` | ❓ 未在解析面列出（语料仅 2 处） |
| 动画纹理 `g_Texture0Rotation/Translation` | ✅ `CPass.cpp:197` | 未见 |

**判定：接近对齐**。语料用到的 shader 为 `genericimage2` 46 / `genericimage4` 38 /
`genericparticle` 20，其余约 25 个为 `effects/*`（含 workshop 自带自定义 shader）；
combo 仅 `VERSION`/`version`/`VERTICAL`/`REFRACT`/`ENABLEMASK`。

### S2 场景图与对象模型 —— 语料压力：高（**本侧最大缺口**）

| 语料字段 | 参考 | 本侧 |
| --- | --- | --- |
| `origin` / `scale` / `size` / `visible` | ✅ `CImage.cpp:156` | ✅ `scene_parse.c:1049` |
| **`angles`（旋转）** | ✅ `CImage.cpp:156` | 🔴 **完全未解析**（`grep angles src/scene*` = 0 命中） |
| **`parent`** | ✅ 带环检测 `CScene.cpp:181` | 🔴 未处理（`scene_parse.c:446` 的 `parent` 只是形参名） |
| **`alignment`** | ✅ `CImage.cpp:242` | 🔴 未处理 |
| **`animationlayers`** | 🟡 解析但渲染端未消费 `ObjectParser.cpp:264` | 🔴 未处理 |
| `solid` / `ledsource` / `copybackground` | 🟡 solid 变占位对象 `ObjectParser.cpp:76` | 🔴 未处理 |
| `disablepropagation` / `locktransforms` | 参考有 parent 语义 | 🔴 未处理 |
| `parallaxDepth` | ✅ `CScene.cpp:304` | 🔴 未处理（`scene_model.h:248` 只有 `parallax` 开关） |
| `color` / `alpha` / `brightness` | ✅ `CImage.cpp:177` | 🟡 `alpha` 有（`scene_parse.c:1102`），`color`/`brightness` 未见 |
| `colorBlendMode` | ✅ `CImage.cpp:751` | 🔴 未处理 |
| `instanceoverride` | ✅ `ObjectParser.cpp:867` | 🟡 有解析（`scene_parse.c` 2 处） |
| `anchor` / `clampuvs` / `depthtest` | ✅ | 🔴 未处理 |
| `dependencies` 渲染顺序 | ✅ `CScene.cpp:262` | ✅ `scene_parse.c:912` |
| `_rt_imageLayerComposite_<id>` | ✅ `CImage.cpp:265` | 🟡 降级（只认 `_rt_FullFrameBuffer`）`scene_image.c:157` |

**量化影响**：`angles` 共 251 个 object 带该字段，其中 **54 个非零（且全部是纯 z 轴旋转），分布在 5/8 个场景**；
`parent` 出现在 183 个 object / 4 个场景；`parallaxDepth` 122 个 / 5 个场景。

### S3 粒子系统 —— 语料压力：中；**本侧对语料用量 100% 覆盖**

| 能力 | 参考 | 本侧 |
| --- | --- | --- |
| emitter `boxrandom` / `sphererandom` | ✅ `CParticle.cpp:355` | ✅ `scene_parse.c:550` |
| 语料出现的 8 个 initializer | ✅ `ObjectParser.cpp:628` | ✅ **全部 8 个** `scene_parse.c:616–722` |
| 语料出现的 6 个 operator | ✅ `ObjectParser.cpp:681` | ✅ **全部 6 个** `scene_parse.c:725–828` |
| renderer `sprite` / `spritetrail` | ✅ `ObjectParser.cpp:761` | ✅ `scene_particle_gl.c:233` |
| `animationmode` sequence/randomframe | ✅ `CParticle.cpp:281` | ✅ `scene_particle_gl.c:173` |
| `flags & 4` 透视粒子 | ✅ `CParticle.cpp:1882` | ✅ `scene_particle_gl.c:212` |
| `REFRACT` combo 的 `_rt_FullFrameBuffer` 复制 | ✅ `CParticle.cpp:1666` | ✅ `scene_gl.c:1877` |
| 参考有而语料**未用**的：`mapsequencearoundcontrolpoint`、`sizechange/alphachange/colorchange/turbulence/vortex`、`rope/ropetrail`、子粒子系统、emitter 定时、音频驱动发射 | ✅ 全部 | 🔴 全部缺失 |

**这是对旧结论的直接修正**：`SIMILARITY.md` 写"双方最大的单文件都是粒子系统，这也是差距最大的地方"。
按语料实际用量，差距**不在**被使用的算子上（本侧 100% 覆盖），而在参考实现了但 8 个真实场景
一个都没用的算子/渲染器上。若后续要投人力，这里优先级应下调。

### S4 文本渲染 —— 语料压力：低（30 个 object / 6 场景）

| 能力 | 参考 | 本侧 |
| --- | --- | --- |
| 字形光栅化 | ✅ FreeType `CText.cpp:231` | ✅ `scene_text.c:127` |
| 字色 | ✅ `uColor` `CText.cpp:288` | 🔴 **桩**：解析了但光栅硬编码 255（`scene_parse.c:1188` + `scene_text.c:127`） |
| 多行 | 🔴 仅单行（`CText.cpp:233` 注释 Phase 1） | ✅ UTF-8 多行 `scene_text.c:52` |
| 对齐 / padding / 富文本 / CJK 整形 | 🔴 未应用 | 🔴 缺失 |
| 脚本驱动 `update(value)` | ✅ `CText.cpp:386` | ✅ `scene_script.c:865` |

语料在这块同时用了 `horizontalalign`/`verticalalign`/`maxwidth`/`limitwidth`/`limitrows`/`pointsize`/`font`（各 30）。
两边都不完整，**本侧在"字色"这一项反而弱于参考**。

### S5 相机与后处理 —— 语料压力：高（8/8 场景开 bloom 与 parallax）

| 能力 | 参考 | 本侧 |
| --- | --- | --- |
| 正交投影 + 自动尺寸 | ✅ `Camera.cpp:42` | ✅ `scene_core.c:42` |
| 透视主相机 | 🔴 未实现（TODO）`Camera.cpp:42`；透视只给 `flags&4` 粒子 | 🔴 同样只给粒子（`scene_gl.c:1914`） |
| **`bloom` 全链路** | ✅ 注入虚拟 `bloomeffect.json` + `_rt_Bloom` `CScene.cpp:104` | 🔴 **桩**：解析+helper，无调用方（`scene_parse.c:1348`、`scene_core.c:94`） |
| **`cameraparallax`** | ✅ `CScene.cpp:304` | 🔴 桩：解析后无消费者（`scene_model.h:248`） |
| `camerashake` | 🟡 解析+暴露给 JS，未见运行时变换 | 🔴 缺失 |
| `_rt_FullFrameBuffer` 场景合成 | ✅ `CWallpaper.cpp:312` | ✅ `scene_gl.c:1877` |
| 场景空间合成 FBO | 参考同 | 🔴 缺失（SPEC 决策 `SPEC.md:149`） |
| `clear/ambient/skylight` 颜色 | ✅ `CScene.cpp:89` | ✅ `scene.c:443` |

**判定：本侧主要缺口。** bloom 在 8/8 语料场景里都开启；`cameraparallax` 也在 8/8。
不过要注意：**参考的相机同样是正交-only**，所以"透视主相机"不是本侧落后，是两边一致。

### S6 效果与 pass（effect.json）—— 语料压力：高

| 能力 | 参考 | 本侧 |
| --- | --- | --- |
| `effect.json`（name/group/passes/dependencies/fbos） | ✅ `EffectParser.cpp:19` | ✅ `scene_parse.c:288` |
| pass 的 `command`（`copy`/`swap`）+ `bind[]` | ✅ `EffectParser.cpp:48,67` | 🟡 仅 `copy` 语义 |
| effect 级 `fbos`（name/format/scale/unique） | ✅ `EffectParser.cpp:99` | ✅ `scene_parse.c:307` |
| ping-pong FBO + `bind:"previous"` | ✅ `CImage.cpp:892`、`CPass.cpp:102` | ✅ `scene_image.c:197–340` |
| 多 pass 时首 pass blending 移到最后 | ✅ `CImage.cpp:769` | ❓ 未见 |
| `colorBlendMode` 注入 `effectpassthrough` | ✅ `CImage.cpp:751` | 🔴 未处理（见 S2） |
| `_rt_Bloom` 生产方 | ✅ `CScene.cpp:104` | 🔴 无（见 S5） |
| 失败降级（不崩、保留上一帧） | — | ✅ `scene_image.c:230,268` |

### S7 脚本（JavaScript）—— 语料压力：**极低**（`.js` 0 个；`instanceoverride.script` 仅 1 处）

| 能力 | 参考 | 本侧 |
| --- | --- | --- |
| QuickJS 运行时 | ✅ `ScriptEngine.cpp:17` | ✅ `scene_script.c:480` |
| `scene.*` / `thisScene.*` / `input.*` / `engine.*` | ✅ `SceneObject.cpp`、`EngineObject.cpp:149`、`InputObject.cpp` | ✅ 子集 `scene_script.c:524–1129` |
| 定时器 | ✅ `EngineObject.cpp:259` | ✅ `scene_script.c:510` |
| `createScriptProperties` 构建器 | ✅ `ScriptPropertiesObject.cpp:144` | 🔴 依赖 project.json 声明 |
| 模块 `WEMath` / `WEColor` / `import` | ✅ `MathModule.cpp:13`、`ColorModule.cpp:13` | 🔴 `import` 被剥离 `scene_script.c:839` |
| `{expression:…}` 动态值 | ✅ 求值 | 🔴 桩：返回常量 `scene_script.c:726` |
| `engine.registerAudioBuffers` | 🟡 由 Pulse 提供真实频谱 | 🔴 桩：返回零数组 `scene_script.c:781` |
| 媒体钩子（`mediaPlaybackChanged` 等） | ✅ `ScriptEngine.cpp:684` | 🔴 缺失 |
| 脚本只读限制 | 参考拒绝 JS 写属性 | 本侧可写（`scene_script.c:1032`） |

### S8 音频 —— 语料压力：中

| 能力 | 参考 | 本侧 |
| --- | --- | --- |
| sound object / loop / volume / mute | ✅ `CSound.cpp:25` | ✅ `scene_sound.c:73,102` |
| **`g_AudioSpectrum{16,32,64}{Left,Right}`** | ✅ `CPass.cpp:885` + Pulse 记录器 | 🔴 无：场景着色器拿不到频谱（`we_audio`/`we_pulse` 只被 `src/web.c` 引用） |
| `supportsaudioprocessing` 参与壁纸选择 | ✅ `ProjectParser.cpp:39` | 不适用 |

**注意**：语料里存在音频驱动着色器 —— `workshop/2084198056/effects/Simple_Audio_Bars`（3 次）、
`workshop/2504730727/effects/audio_ring`。即音频响应在真实场景里**确实被使用**，
但只在 `web` 后端接入了频谱，scene 后端没有。

### S9 puppet / 3D 模型 —— 语料压力：**高**（19/19 模型都是 puppet）

| 能力 | 参考 | 本侧 |
| --- | --- | --- |
| `MDLV0021` / `MDLV0023` 网格读取 | ✅ `CImage.cpp:441` | 🔴 无 |
| `MDLV0016` | 🔴 **参考自身不支持**（源码只认 0021/0023；运行时实证报错） | 🔴 无 |
| 静态索引绘制（`glDrawElements`） | ✅ `CImage.cpp:548` | 🔴 只画贴图四边形 `scene_image.c:143` |
| 骨骼 / 形变 / `animationlayers` 动画 | 🔴 参考也未应用（网格位置静态 `CImage.cpp:472`） | 🔴 无 |

**关键定量结论**：语料 19 个模型全部是 puppet，其中 **6 个是 `MDLV0016`**，
**参考实现自己也渲染不了这 6 个**（`Unsupported puppet model header MDLV0016`）。
所以这一格的差距是"参考：13/19 模型可静态渲染、0 动画；本侧：0/19"。

### S10 包与资源加载 —— 语料压力：高

| 能力 | 参考 | 本侧 |
| --- | --- | --- |
| PKGV 解析 | ✅ `PackageParser.cpp:15` | ✅ `scene_pkg.c:35` |
| `.tex` `TEXV0005`/`TEXB0001–0004` + LZ4 mip | ✅ `TextureParser.cpp:185` | ✅ `scene_tex.c:170,337` |
| DXT1/3/5 + R8/RG8/RGBA8 | ✅ `Texture.h:70` | ✅ `scene_tex.c:299` |
| 纹理格式总面（BC7/RG1616f/RGBa1010102/…） | ✅ 多 | 🟡 子集 |
| TEXS 精灵表 | ✅ `TextureParser.cpp:275` | ✅ `scene_tex.c:119` |
| 虚拟文件系统 | ✅ `Container.cpp:26` | 🟡 无 VFS：整包解到 `/tmp/xw-scene-XXXXXX` `scene.c:337` |
| GIF / 动画纹理 | ✅ `CTexture.cpp:122` | 🔴 无 |
| 路径逃逸防护 | ✅ `Directory.cpp:12` | ✅ `scene_pkg.c` 拒绝 `..` |

### S11 场景内视频 —— 语料压力：低但真实（1/8 场景）

| 能力 | 参考 | 本侧 |
| --- | --- | --- |
| `TEXB0004` + `FIF_MP4`/`TextureFlags_Video` 纹理 | ✅ `TextureParser.cpp:213` | 🔴 显式报错 `scene_tex.c:222` |
| libmpv 解码进 GL 纹理 | ✅ `CTexture.cpp:32` | 🔴 无 |
| 运行时验证 | ✅ 本次捕获观测到 libmpv 输出与 1440x2560 h264 视频流 | — |

### S12 用户属性与配置 —— 语料压力：中

| 能力 | 参考 | 本侧 |
| --- | --- | --- |
| 属性类型 boolean/slider(step)/combo(color/bool/text) | ✅ `PropertyParser.cpp:7,102`（运行时实证见 §4） | 🟡 解析类型但无类型语义校验 `scene_parse.c:1503` |
| `user` 绑定 + `--set-property` | ✅ | ✅ `scene.c:820` |
| 条件（`user:{name,condition}`） | 🟡 解析进 `ConditionInfo`，但**未找到求值代码路径** | 🔴 未处理 |
| 属性持久化 | 参考由 WE 侧 | 🟡 仅内存，重启丢失 `scene_script.c:177` |

---

## 6. 实现度评估

方法：按各子系统对"典型 scene 画面"的贡献给权重（权重是判断，不是测量），
本侧得分用 §5 矩阵中**语料实际用量**口径的覆盖率。原始事实见 §3/§5，权重可按需重算。

| 子系统 | 权重 | 本侧覆盖率 | 贡献 |
| --- | --- | --- | --- |
| S1 shader/material | 18% | ~95% | 17.1 |
| S2 场景图/对象模型 | 14% | ~55% | 7.7 |
| S3 粒子 | 14% | ~100% | 14.0 |
| S4 文本 | 4% | ~60% | 2.4 |
| S5 相机/后处理 | 14% | ~45% | 6.3 |
| S6 效果/pass | 10% | ~85% | 8.5 |
| S7 脚本 | 8% | ~70% | 5.6 |
| S8 音频 | 5% | ~55% | 2.8 |
| S9 puppet/3D | 5% | ~10% | 0.5 |
| S10 包/资源 | 5% | ~90% | 4.5 |
| S11 场景内视频 | 3% | ~0% | 0.0 |
| **合计** | 100% | | **≈69%（区间 65–70%）** |

与旧报告 66% 落在同一区间，但**差距分布完全不同**：

| | 旧 `SIMILARITY.md` | 本次（语料口径） |
| --- | --- | --- |
| 最大差距 | 粒子、脚本、文本、相机/合成顺序 | **S2 对象模型（旋转/父子/对齐）、S5 bloom+parallax、S9 puppet、S11 场景内视频** |
| 粒子 | "差距最大" | **语料用量 100% 覆盖**，只剩未使用的算子 |
| 相机 | "相机/合成顺序" | 参考也是正交-only；真实差距是 **bloom 未落地**（8/8 场景开启） |
| 脚本 | 列为四大差距之一 | 语料里 `.js` **0 个**、`script` 绑定 1 处 → 优先级应下调 |

### 6.1 本次新增的、旧报告没有的结论

1. **`angles` 完全没实现**：251 个 object 带该字段、54 个非零旋转（全是 z 轴）/ 5 个场景。
   旧报告未提及。这是最容易修、影响面很明确的一项。
2. **`parent` / `alignment` / `parallaxDepth` / `solid` / `locktransforms` / `disablepropagation` 均未处理**，
   而这些在语料里出现 64–205 次、覆盖 3–5 个场景。
3. **参考实现自己也不支持 `MDLV0016`**（运行时实证），而语料 19 个模型里有 6 个是它 ——
   对标时不应把这 6 个算作"本侧落后"。
4. **参考的相机也是正交-only**，透视只用于 `flags&4` 粒子。旧报告把它归入本侧差距是不准确的。
5. **语料里存在音频驱动着色器**（`Simple_Audio_Bars`、`audio_ring`），
   而本侧 scene 后端完全拿不到频谱（只有 web 后端接了 `we_pulse`）。
6. **`scene.pkg` 是私有 `PKGV` 容器**（版本 0008/0018/0021/0022/0023/0024），不是 zip —— REA 拒绝解析。
7. **文本字色在本侧是桩**（硬编码 255），这一项本侧弱于参考。

### 6.2 建议的投入顺序（按语料加权影响 × 修复成本）

1. `angles` 旋转（54 个非零对象 / 5 场景，改动局促）
2. bloom 落地（8/8 场景开启，参考已有可对标的注入式实现）
3. `parent` + `alignment`（183 / 64 次，4 / 3 场景）
4. 场景频谱 → `g_AudioSpectrum*` 复用已有的 `we_pulse`（补齐后端接线）
5. `parallaxDepth` / `colorBlendMode` / `brightness` / `solid`
6. puppet 静态网格（`MDLV0021/0023`，参考也没有动画，按静态绘制即可对标）
7. 场景内视频（只影响 1/8 场景）
8. 粒子未使用算子、脚本模块系统 —— 语料无需求，可暂缓

---

## 7. 未验证 / 残余未知

- **没有像素级对比**：旧报告的 ground-truth 逐像素基准本次**未复现**；§6 的百分比是能力矩阵加权，
  不是实测相似度。想看画面结论必须另做同帧对比。
- **`angles` 的影响未实测**：只统计到 54 个非零对象，未渲染确认视觉效果偏差。
- **本侧"已实现"未逐条运行验证**：`src/scene*.c` 中标识为 IMPLEMENTED 的能力，
  依据是解析+调用路径存在，不等同于行为正确。
- **参考实现的条件求值**：解析进 `ConditionInfo` 后未找到执行路径，**未能确认**参考是否真的求值 `condition`。
- **`g_Daytime` 来源未追踪**。
- **本侧构建开关**（`XW_HAVE_GL`/`XW_HAVE_SDL_MIXER`/`XW_HAVE_FT`/`XW_HAVE_LZ4` 等）未确认，
  代码里存在 `#ifdef` 降级分支，实际启用情况未查。
- **REA 无法触及的部分**：参考的 C++ 实现结构一律未经反编译验证；
  若需二进制级确认（例如确认已安装 `.so` 与源码一致），需要 `GHIDRA_INSTALL_DIR` 指向
  真实 Ghidra 12.1.x 安装。

---

## 8. 复现

```bash
# REA 运行时证据（Xvfb 隔离，不影响当前桌面）
xvfb-run -a --server-args="-screen 0 640x480x24" \
  linux-wallpaperengine --list-properties --window 0x0x640x480 2889253135
xvfb-run -a --server-args="-screen 0 640x480x24" \
  linux-wallpaperengine --dump-structure --window 0x0x640x480 --fps 1 2889253135

# 语料解包（布局见 src/scene_pkg.c:35）
#   u32 strlen | "PKGV"+4 位版本 | u32 条目数
#   每项：u32 名字长度 | 名字（无 padding） | u32 数据偏移 | u32 数据长度
#   数据基址 = 文件表末尾；偏移相对基址
# 语料位置
~/.local/share/Steam/steamapps/workshop/content/431960/*/scene.pkg
```

REA 证据 ID：

- `ev_d4b00494ca9d24fc596527bc1625a8cd2bee990c90e06321a5798679fd113eff` —— `--list-properties`
- `ev_65d3bcc8e760cdaf390596c45ad512532f9974539d29d772d05c079d90b46b18` —— `--dump-structure`
