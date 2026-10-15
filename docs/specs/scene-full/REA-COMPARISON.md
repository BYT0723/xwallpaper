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
| 原生反编译（`open_binary` → `analyze_function` / `procedure_pseudo_code`） | ❌ → ✅ | 初次：`binary_session` 报 `ghidra: not_configured`（`GHIDRA_INSTALL_DIR` 未设）、`hopper: executable_missing`、`ida: not_configured`。**后续装上 Ghidra 12.1.2 后已打通**，见 §9 |
| `inspect_binary_layout`（离线 ELF 布局/符号/重定位） | ❌ | `provider_unavailable`：`Set REA_PWNTOOLS_PYTHON to an absolute caller-supplied Python executable with pwntools 4.15.0, pyelftools 0.33 and Unicorn 2.1.2` |
| `inspect_artifact` 打安装目录 / 打 `scene.pkg` | ❌ | 目录：`target_unavailable … directory_requires_file`；`scene.pkg`：`unsupported binary format`（PKGV 私有容器，非 zip） |

因此初稿**没有代码级反编译证据**：涉及参考实现内部结构的结论，来源是其 GPL 源码 + 运行时观测。
§9 在装上 Ghidra 之后补上了二进制级取证。

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
| 已安装二进制 | `/usr/bin/linux-wallpaperengine`（150 B wrapper）→ `/opt/linux-wallpaperengine/linux-wallpaperengine`（31 KB launcher）→ `liblinux-wallpaperengine-lib.so`（10.3 MB，sha256 `16567497…2c27`） | `.symtab` 已 strip，但 **`.dynsym` 保留 1,754 个 WallpaperEngine C++ 符号**（可完整 demangle）；静态链 QuickJS |

> **更正（2026-10-15）**：本报告初稿断言该 `.so` "C++ 符号隐藏可见性、反编译收益远小于读源码"，
> 据此把"以源码为准"当成技术必然。**该断言是错的**：`.dynsym` 完整保留了
> `WallpaperEngine::*` 的类名与方法名（见 §9），足以在不解码的前提下核对编译进去的特性面。
> "以源码为准"仍然成立，但理由只是**效率**，不是"拿不到"。

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

### S5 相机与后处理 —— 语料压力：中（`cameraparallax` 8/8；`bloom` 字段 8/8 但**仅 2 个场景真正开启**）

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

**判定：仍有缺口，但没有初稿说的那么大。** `cameraparallax` 在 8/8 场景出现；`bloom` 字段
虽然 8/8 出现，但 `false` 的有 6 个，**真正开启的只有 2/8**（`2887099508`、`3422426571`，
且都通过用户属性绑定，见 §10.1 的更正）。另外要注意：**参考的相机同样是正交-only**，
所以"透视主相机"不是本侧落后，是两边一致。

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
| 相机 | "相机/合成顺序" | 参考也是正交-only；真实差距是 **bloom 未落地**（**2/8** 场景开启，初稿写成 8/8，见 §10.1） |
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
2. bloom 落地（**只有 2/8 场景开启**，初稿误记为 8/8，见 §10.1 —— 优先级应下调）
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
- **二进制与源码的差异（未解决）**：源码 `CImage.cpp:442` 同时比较 `"MDLV0021"` 与 `"MDLV0023"`，
  但已安装 `.so` 中**只存在 `MDLV0021` 的字面量**（`MDLV0023` 字节不存在，见 §9.5）。
  可能是编译器对比较做了不同物化，也可能是 `.so` 与检出的源码不完全同版。**未能确定**。
- **REA 字符串清单不等于二进制全部文本**：Ghidra 只把"已定义数据"算作字符串。
  `MDLV0021` 在二进制里是 **`.text` 中的 8 字节立即数**（`movabs`），因此 `search_strings` 搜不到；
  也就是说该清单里的**缺席不能证明二进制里没有**。本次所有字符串结论都据此校准过。

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
- `ev_72c9599048bd19f901bada8b43034ddfbcc62b536da0477588270b355b886a2a` —— Ghidra `search_strings`（§9）

二进制级取证（§9；Ghidra 需先装好并设置 `GHIDRA_INSTALL_DIR`）：

```bash
export GHIDRA_INSTALL_DIR=/opt/ghidra
export REA_GHIDRA_STARTUP_TIMEOUT_MS=1800000   # 大二进制必须放宽启动期限
SO=/opt/linux-wallpaperengine/liblinux-wallpaperengine-lib.so

# 概览 + 落盘 snapshot（本次约 5.5 分钟）
npx -y rea-agents@6.5.0 analyze "$SO" --snapshot /tmp/rea-snap --format json

# 字符串取证（约 6 分钟/次）
npx -y rea-agents@6.5.0 search "$SO" --kind strings --mode regex \
  --pattern='(velocityrandom|alphafade|vortex|MDLV|LightingV1)' \
  --snapshot /tmp/rea-snap --format json

# .dynsym 里的 C++ 名字面（无需 Ghidra，快速交叉核对）
nm -D --defined-only "$SO" | awk '{print $NF}' | grep '^_Z' | c++filt
```

---

## 9. 二进制级取证（装上 Ghidra 之后的补充）

初稿发布后补装了 Ghidra，本节的结论**直接来自已安装二进制**，不再依赖源码推断。

### 9.1 环境与目标

- provider：Ghidra 12.1.2（`/opt/ghidra`），native decompiler `linux_x86_64`，Java 27
- 目标：`/opt/linux-wallpaperengine/liblinux-wallpaperengine-lib.so`
  sha256 `16567497fd47433c9d43b35f4b7cf8b80085b15d3facfd30a620018503212c27`
- Ghidra 分析规模：**19,157 个 procedure、15,319 条字符串、32 个 segment**
- 注意：REA 要求 Ghidra 默认自动分析跑完才提供服务；该二进制一次需约 **5.5 分钟**，
  超过 MCP 默认 330 s 的 startup 期限，需要 `REA_GHIDRA_STARTUP_TIMEOUT_MS`（本次设 30 min）
- REA 证据：`ev_72c9599048bd19f901bada8b43034ddfbcc62b536da0477588270b355b886a2a`
  （`search_strings`，Ghidra 12.1.2，`authority: shipped-artifact`）

### 9.2 版本同一性：**成立**

二进制内含 CEF 版本字符串 `135.0.17` 与 `chromium-135.0.7049.52`，
与源码 `CMakeLists.txt:49` 的 `CEF_VERSION "135.0.17+gcbc1c5b+chromium-135.0.7049.52"` 完全一致。

→ §5 全部"以源码为准"的对比**对得上已安装的这份二进制**，不需要推翻。

### 9.3 符号表：`.dynsym` 保留了完整 C++ 名

尽管 `.symtab` 被 strip，`.dynsym` 内有 **1,754 个 `WallpaperEngine::*` 符号**，可完整 demangle。
按命名空间分布（前几名）：`Data::Model` 491、`Render::Drivers` 325、`Render::Objects` 302、
`Application::ApplicationContext` 121、`Render::Wallpapers` 87、`Audio::Drivers` 74、
`Scripting::Modules` 66、`Scripting::Adapters` 57、`ScriptEngine` 55、`WebBrowser::CEF` 64、
`VideoPlayback::MPV` 39、`Render::CFBO` 43、`Render::CTexture` 35。

### 9.4 粒子面：编译进去的算子/初始化器（**这是对源码结论的独立验证**）

`CParticle::create*` 工厂方法共 23 个，即实际编译进二进制的全集：

| 类别 | 编译进去的 |
| --- | --- |
| initializer（9） | AlphaRandom、AngularVelocityRandom、ColorRandom、LifetimeRandom、MapSequenceAroundControlPoint、RotationRandom、SizeRandom、TurbulentVelocityRandom、VelocityRandom |
| operator（12） | AlphaChange、AlphaFade、AngularMovement、ColorChange、ControlPointAttract、Movement、OscillateAlpha、OscillatePosition、OscillateSize、SizeChange、Turbulence、Vortex |
| emitter（2） | Box、Sphere |

与 §3 的语料用量交叉：语料实际用到的 8 个 initializer、6 个 operator、2 个 emitter
**全部在这份编译清单里**。§5/S3 的判定（本侧对语料用量 100% 覆盖）成立。

另外，二进制里以独立字符串存在的 renderer 名只有 `ropetrail`、`spritetrail`
（`boxrandom`/`sphererandom` 同理）——`sprite` 未作为独立字符串出现，与"默认渲染器"的实现方式一致。

### 9.5 puppet：**静态网格有，动画没有**（二进制确认）

`CImage` 的方法里与 puppet 相关的只有三个：
`loadPuppetMesh`、`setupPuppetGeometryCallback`、`updatePuppetPositionBuffer`；
另有 `updateGeometryBuffers` / `uploadGeometryBuffers` / `resolveGeometrySize`。

**没有任何骨骼、骨架、形变相关符号**，也没有 `animationlayers` 的运行时消费。
→ §5/S9 的结论（参考只做静态索引绘制、不应用动画）在二进制层面成立。

`MDLV` 版本判定：二进制中只有 `MDLV0021` 的字面量；运行时对语料里 6 个 `MDLV0016`
模型报 `Unsupported puppet model header MDLV0016 in models/Rin_puppet.mdl`。
→ **语料 19 个模型里 6 个参考自己也加载不了**，这条不受"符号隐藏"质疑影响。

### 9.6 其它子系统在二进制中的存在性

| 子系统 | 二进制证据 |
| --- | --- |
| 场景内视频 | `VideoPlayback::MPV::GLPlayer`（`init`/`prepareGL`/`render`/`play`/`setMuted`/`incrementUsageCount`…）39 个符号 |
| 音频频谱 | `PulseAudioPlaybackRecorder`、`PlaybackRecorder`、`getRecorder`（Pulse 回调齐全） |
| 脚本 | `Scripting::ScriptEngine` 55 + `Modules` 66（`ColorModule`/`MathModule`）+ `Adapters` 57；`EngineObject` 暴露 `clearTimeout`/`clearInterval`/`reserveNextTimeoutId`/`tick` |
| 文本 | `CText`：`initFreeType`/`loadEmbeddedFont`/`loadSystemFont`/`initScriptLayer`/`rebuildTextureFrom`/`computeEffectivePixelSize`/`uploadQuadVertices` |
| 图像对象 | `CImage`：`getBrightness`/`getColor`/`getColor4`/`getCompositeColor`/`getUserAlpha`/`pinpongFramebuffer`/`configurePassTarget` |
| Web 后端 | `WebBrowser::CEF` 64 个符号（CEF 135 内嵌） |

### 9.7 §9 小结

二进制级取证**没有推翻 §5/§6 的任何判定**，反而独立确认了其中最关键的三条：
粒子的语料覆盖、puppet 的静态-only、场景内视频与音频频谱的存在。
唯一被推翻的是**我在初稿里对本二进制可分析性的判断**（见 §2 的更正）。

---

## 10. 后续更正与进展

### 10.1 更正：bloom 不是 8/8 开启

初稿在 §5/S5、§6、§6.2 里写"bloom 在 8/8 语料场景里都开启"。**这是错的**：字段在 8/8 场景
都出现，但值为 `false` 的有 6 个，**真正开启的只有 2/8**：

| 场景 | bloom | strength |
| --- | --- | --- |
| 2887099508 | `{user: highlightneedpostprocessinginset, value: true}` | 0.04 |
| 3422426571 | `{user: newproperty1, value: true}` | 0.16 |
| 其余 6 个 | `false` | 2.0（未启用） |

原因是我把"字段存在"当成了"已启用"。S5 的权重与投入顺序据此下调，上文的对应处已改。

### 10.2 进展：放置（placement）已修复

初稿 §6/§7 把放置列为"实测净亏、原因未知"的死路，这是当时的事实。**它现在修好了，
而且根因不是那个 A/B 所指向的方向。**

**根因**：本侧**根本没有逐对象定位**。基础绘制走 `draw_full`，它的 quad 是全屏 `-1..1`，
所以每个图像对象都被拉伸铺满整个窗口，只有在恰好占满画布时才看着正常。

受控实验（不靠读代码）：1280x720 画布、黑底、一个 200x200 的方块在 (200,200)、纹理 64x64
→ **红色像素占比 = 1.0，整屏全红**。

**修了两处**，正好对应 29610ce 记录的两个症状：

1. **FBO 从不清空** —— 只画物体矩形，上一物体的像素就留在周围。这正是那条
   "a bright strip shows at the right edge"。现在基础绘制前先把 FBO 0 清成全透明。
2. **无纹理对象拿窗口宽度当 quad 尺寸** —— 参考的 `CImage::getSize()` 返回的是**声明的
   `size`**，而 compose/project 层正是这一类（`3808922316` 的 projectlayer 是 3840x2160、
   origin 0，本该由它铺满画布）。这就是把壁纸变成一堆窄条的原因。

**验证**（对照手算，不是全帧差异）：

```
预测: left = 200 - 64/2 = 168,  top = 720 - 200 - 64/2 = 488,  尺寸 64x64
实测: 红色包围盒 64x64 起点(168,488)，红色占比 0.00444 = 64*64/(1280*720)
```

**更正（同日）：上面这段"更接近参考"是伪影，已作废。**

我最初写的是"未放置 RMSE 0.27766 -> 已放置 0.26391，更接近参考"。那是拿**带噪声的
捕获**（改前的 `xwshot` 运行）和**确定性捕获**比出来的，不是放置的效果。

用进程内 tick 做确定性捕获后（与 `tests/integration/test_scene_gl.c` 同样驱动窗口，
40 tick + `XW_FIXED_TIME`；同一场景两次运行**逐字节相同**），实测是：

```
2178468632   before vs after 逐字节相同
2889253135   before vs after 逐字节相同
3422426571   before vs after 逐字节相同
3497575235   before vs after 逐字节相同
```

四个场景放置**完全没有影响**。既是逐字节相同，对参考的 RMSE 也必然相同
（实测两边都是 0.263913）。

**所以放置修复的性质是**：它修正了一个真实的缺陷（受控 fixture 证明：改前整屏全红，
改后是手算预测的 64x64 于 (168,488)），但在**这些**真实壁纸上它是 no-op —— 因为这些
壁纸的图层纹理足够大，算出的矩形本来就覆盖整个窗口。只有纹理小于画布的壁纸才会变，
而这类里我手上唯一有基准的 `3808922316` 参考自己崩溃（§10.5），无从判断好坏。

**它也不该被算作"语料变好了"**：没有证据支持。它的价值是**正确性**与后续合成层工作的
前置条件，不是收益。

**边界**：`3808922316` **没有基准可比** —— 参考实现自己渲染不了它，直接抛
`json.exception.type_error.302: type must be number, but is string` 中止。所以那张
"变稀疏"的画面既不能说变好也不能说变坏。

### 10.3 §6 的实现度估计应上修

§6 的 65–70% 是在"放置死路 + bloom 8/8 未落地"的前提下给的。两项都需要修正，
但方向不同：

- bloom 的真实压力只有 2/8，S5 的权重应**下调**；
- 放置修复让 S2 那族的**路径**通了（`angles`、`parent`、`alignment`、`parallaxDepth`
  都走这条路径），但 §10.2 的更正说明：在测过的四个语料壁纸上它是 no-op。
  "路径通了"不等于"这些场景因此变好了"—— 会不会变好取决于那些壁纸的图层尺寸，
  尚未测到。

本次没有重跑打分（作者的评分工具不在仓库里），所以**不给出新的百分比**：
§6 的表格是判断加权，改动权重后应当上修，但具体数值需要那套工具才能给出。

### 10.4 本次会话在此之后已落地的改动

- 对象 `color` 着色（`ColorBuilder::parse` 的语义，接在基础拷贝上，像素级 RED/GREEN 验证）
- MDLV puppet 静态网格（解析器对真实语料验证；绘制按 `updatePuppetPositionBuffer` 的算法）
- 场景内视频（`.tex` 里的 MP4 交给 libmpv render API，`3610097897` 从整张空白到正常渲染）
- 音频频谱接进 scene 着色器（并修掉一个共享 Pulse 引用的既有 bug）
- 放置修复（见 §10.2）

### 10.5 参考实现的边界：它无法加载 3808922316

§10.2 提到 `3808922316` 没有基准，因为参考实现自己崩了。**已定位到具体原因**。

现象：

```
terminate called after throwing an instance of 'nlohmann::json_abi_v3_11_3::detail::type_error'
  what():  [json.exception.type_error.302] type must be number, but is string
```

**定位过程**（缩减法）：参考可以直接吃目录，只差 `project.json`。补上后复现，
然后二分 `scene.json` 的 objects：

```
 1..120  OK
121..123  OK
124       CRASH   <- 触发对象
```

第 124 个是一个**文本对象**：

```json
{ "font": "fonts/Tourner (100).ttf", "text": {"user": "newproperty5", "value": "HACHIWARE ..."},
  "padding": "32.00000 32.00000", "spacing": "8.00000 0.00000", "pointsize": 26.0 }
```

**根因**：参考把 `padding` 当**标量 int** 读 —— `ObjectParser.cpp:139`
`.padding = it.optional("padding", 0)`，模型是 `Object.h:614` 的 `int padding;`
—— 而语料里的 `padding` 是 **2 分量字符串**。nlohmann 对字符串调 `.get<int>()`
就抛这个错。

字段级确认（只留该对象，其余不变）：

```
A  带 padding     CRASH
B  去掉 padding   OK
```

**所以这是参考实现自己的 bug**：它无法加载 `padding` 写成向量字符串的文本对象。

**对本侧的启示**：`xwallpaper` 根本不解析 `padding`（`scene_parse.c` 零命中），
直接忽略，因此**能渲染这张参考渲染不了的壁纸**。这也是 §6 里"实现度"口径的一个提醒：
对标时不应把参考的能力当作上界 —— 它在真实语料上也有加载不了的包。

### 10.6 可复现的逐区域比对：方法，与它查出的第一个缺口

§10.2 的更正说明"全帧对比"在本仓库不可靠。**现在有了可靠的做法。**

**确定性捕获**：不驱动守护进程、不用墙钟等待，而是像 `tests/integration/test_scene_gl.c`
那样在进程内驱动窗口 —— 初始化 scene backend、精确 `tick()` 40 次、`XW_FIXED_TIME=1`、
`XGetImage` 落 PPM。自检：同一场景两次运行**逐字节相同**。

**与参考的相位问题，以及为什么它不致命**：参考的场景时钟是 `glfwGetTime()`
（`GLFWOpenGLDriver.cpp:85` —— **墙钟**，不是帧计数），所以它的动画相位**不可精确复现**，
靠像素对齐做不到。但实测一个**相位不敏感**的度量是可行的：把两图各切 16x16 网格、
比每格平均色。在 `2889253135` 上，相位差极大的两次捕获（fps=1000/40 tick 与
fps=30/150 tick）给出**完全相同**的结果（均值 15.5%，最差格也一致）—— 说明差异是
结构性的，不是动画。

**它查出的第一个缺口**：`2889253135` 的 16x16 网格平均单元色差 **39.5/255（15.5%）**，
且差异**集中在一个区域**（gx 9–12、gy 4–5）。切图看，那里参考是深色（43–59）、我们是
近白（251–255）—— 参考有**角色的头发/发髻**，我们只有背景。**这一层缺失。**

**不是本会话引入的**：用改动前的构建（视频切片时期）比同一区域，同样缺失。

**线索**：该场景的 puppet 模型全是 `MDLV0016`（`Rin_puppet.mdl` 等 6 个），而
`MDLV0016` 参考和本侧的解析器都拒绝（§5/S9）。参考拒了却仍能出角色，说明它退回用
材质纹理的平面 quad 画出了那一层；本侧同样有这条退回路径，但没有画出内容 ——
**下一个要查的就是这里**：那个对象的纹理/材质在本侧为什么没落到画面上。

### 10.7 §10.6 那个"缺失的角色层"查清了：角色画得出来，是被过亮的 flare 图层盖住

**结论：角色一直在画，而且和参考几乎一致。** 缺的是别的东西。

**排查路径**（每次都靠确定性捕获 + 缩小对象集）：

1. 该场景只有 **1 个** puppet 对象：`id=49 Rin`，材质纹理 `materials/Rin-puppet.tex`
   （**内嵌 PNG**，1500x2250，能正常解码）。
2. 插桩显示它**被触达、纹理解析并上传成功**，不在任何失败点 —— 而且 `xw_scene_image_draw`
   每帧只被调用 **6 次**，其余 18 个对象是粒子/声音，走别的分支。
3. 只留 `Rin` 一个对象 → **有内容**（stddev 0.29）。说明对象本身画得出来。
4. 逐步加回对象：`[Rin]` → `[Rin_BG, Rin]` → `+Post-Processing Layer` → `+Full Composition
   Layer`，角色区域 stddev 从 0.171 升到 0.214 并保持不变 —— **合成层不吞它**。
5. 按数量缩减全量场景：`N=12` 有内容、`N=16` 变亮。差的是对象 13–16：`Full Composition
   Layer` + `1st/2nd/3rd flare`。**flare 是粒子对象**，在这个区域过亮。

**决定性证据**：只留 `[Rin_BG, Rin]` 两个对象渲染，角色**完整出现** —— 围巾、兜帽、发髻、
耳朵、脸部位置与参考一致：

```
参考            本侧(仅 Rin_BG+Rin)
角色 + 彩虹flare  同一角色，形状/尺度/位置一致，只少 flare 与 bokeh
```

**所以**：

- puppet 的兜底路径是对的。参考拒绝 `MDLV0016` 后退回"材质纹理的平面 quad"，本侧同一条
  路径也画了出来 —— 两边行为一致。
- **角色的位置与尺度是对的**，这是目前关于放置与对象模型最强的证据。
- 全量场景里"角色不见"的真正原因是**别的图层（过亮的 flare 粒子）盖住了那个区域**，
  下一个要查的是**粒子/flare 的亮度与尺寸**，不是图像路径。

**可复现的工具**（都是临时脚本，未入库；重建方法）：
- 确定性捕获：像 `tests/integration/test_scene_gl.c` 那样在进程内驱动窗口，
  `tick()` N 次 + `XW_FIXED_TIME` + `XGetImage` 写 PPM。同场景两次逐字节相同。
- 相位不敏感比较：两图各切 16x16、比每格平均色（§10.6）。

### 10.8 §10.7 的更正，以及定位到的确切缺陷：两个连续合成层会打坏画面

**先更正 §10.7 的一句**："合成层不吞它" 是错的。当时我用**空格**分隔对象名，而
`Post-Processing Layer` 是多词名字，根本没被选进子集 —— 所以第 3、4 组实际与第 2 组
是同一个对象集，才得出"不变"。修正分隔符后结论完全相反。

**确切缺陷**：这个场景有两个 `_rt_FullFrameBuffer` 合成层 —— `Post-Processing Layer`
（`models/util/fullscreenlayer.json`）与 `Full Composition Layer`
（`models/util/projectlayer.json`）。用确定性捕获逐个组合测得：

| 对象集 | 角色区域 mean / stddev |
| --- | --- |
| `[Rin_BG, Rin]` | 0.318 / 0.214 ← 角色正常 |
| `+ Post-Processing Layer` | 0.318 / 0.214 ← **无影响** |
| `+ Full Composition Layer` | 0.318 / 0.214 ← **无影响** |
| `+ 两个都有` | **0.925 / 0.112 ← 角色被盖掉** |

从全量场景反向剔除也一致：删掉**任一**个都恢复 0.318（因为剩下的那个单独无害）。

**所以缺陷是"两个连续的合成层"**，不是任一层本身。合成层的职责是"把当前帧缓冲再合成
一遍"（语义上接近 no-op，只叠加它自己的 effect），本侧走
`xw_gl_framebuffer_snapshot` + `xw_gl_draw_window_source` + `xw_gl_set_shader_quad`
这条路。两个连起来就打坏画面，最可能是**帧缓冲快照与翻转在连续两轮里互相抵消/叠加**
（`xw_gl_bind_target` 的注释正是"any number of FBO round trips keeps the image upright"
—— 那句话在这里不成立）。

**这也是"角色看起来缺失"的最终答案**：角色一直在画，是第二个合成层把它盖住了。

**证据强度**：本节的每个数字都来自同一个确定性捕获（同场景两次运行逐字节相同），
对象集由名字精确匹配（`|` 分隔）选定，并与从全量反向剔除的结果互相印证。

### 10.9 workshop 纹理、负 scale 与 colorBlendMode（2889253135：15.5% → 3.5%）

**链条（一环扣一环）**：底部暖色带差得最多 → 两个 flare 对象材质纹理解析失败
→ 修解析（`workshop/<id>/<name>` 按参考实现应先试 `materials/` 前缀，
`AssetLocator::texture` 即如此）→ 纹理能解出来了，全帧变平（33%）
→ 探针显示 `c7884` 算出负矩形 `rw=-1243`，`draw_rect_full` 拒画返回 -1，
整帧夭折后 `paint_fill` 用 clearcolor 刷平（`(180,188,218)` 恰为
`general.clearcolor`）
→ 负 scale 按参考（`CImage.cpp:234`，反转顶点、同 UV）应为镜像，改矩形取
幅值 + 采样镜像 → 黑盒子盖住角色脸（20%）
→ 黑盒纹理本身是不透明黑底小彩虹弧；参考跑同场景无盒 → 这两个对象的
`colorBlendMode`（7 screen、6 lighten；3808922316 用 2 multiply ×9）经
`common_blending.h:ApplyBlending` 使黑色透明 → 实现 2/6/7 三档合成 → **3.5%**。

验证链：参考 `--screenshot`（默认 lensflare=true）无盒；`--set-property
lensflare=false` 只拿掉弧光与整体提亮（粒子相位会变，A/B 不完全干净，
只看弧光有无）；纹理解包确认 `c7884` 有效区 alpha 全 255。

**教训**：集成测试一度全黑，查了两小时 GL，最后发现是 fixture 自己的 bug
（64×64 纹理按"纹理尺寸优先"只画 96×96 小盒，盒下快照是黑 clearcolor 而非
蓝 base —— skill 说的"先怀疑测试本身"是成立的）。fixture 改成足尺寸纹理后
4/4 通过，且撤掉 src 修复会变红。

状态：`1695f5a`（代码+回归测试），门禁 `make test` 18 套、
`make test-xvfb` 16 组、`make check-client-libs` 全绿。残余最大差距在底部
暖色带（粒子相位对不上系预期内）与头部高光一格。

### 10.10 严重回归：未产出的渲染目标被回退成 util/white，整屏变白

**症状**：用户的 scene 壁纸大面积"不显示/不动"——屏幕是**冻结的全白**。

**根因**（`81fad3f` 引入）：effect pass 声明了引擎自建、我们未产出的渲染目标
（`_rt_imageLayerComposite_<id>_a` 等，参考实现按 image 对象逐个创建），
我们从 `fill_slot` 拿不到，就回退到**着色器注解里的默认纹理**；blend 效果的
`g_Texture1` 默认是 `util/white`，于是该层与白色混合 → 整帧变白；而合成层又会
再次采样这一帧，白色**自我维持**，逐帧稳定。

旧代码其实有一条"跳过该 pass、保留基础图像"的兜底，但条件是
`if (!slots[t].tex && ...)` —— 默认纹理已经把 slot 填上了，条件永不成立。

**修法**：把判断挪到默认纹理查找**之前**。声明的 `_rt_`/`_alias_` 引用只有在
"我们确实能产出"时才可用（`_rt_FullFrameBuffer`，或被某个 pass bind 命名），
否则跳过该 pass、保留基础图像。

**证据链**（每步都是可复现的实测，而非推断）：
1. daemon 下 `2178468632` 屏幕 mean=1 sd=0，6 帧逐字节相同（冻结全白）；
2. 逐对象探针：`Compose` 之前窗口中心 (13,65,127) 夜蓝，之后变 (255,255,255)；
3. 快照探针：第一个 Compose 的快照**正确**（(13,65,127)），第二个 Compose 的快照已是白；
4. FBO 探针：基础拷贝后 FBO0 = (10,64,126) 正确，但 `draw_alpha` 前同一句柄读回 (255,255,255)
   —— 中间只有 effects 循环；
5. 循环探针：`broken=0`、`slots1=7` —— 纹理槽被**默认纹理**填上了，pass 真的执行了。
6. 参考实现渲染同场景 mean=0.2051，我们也应如此。

**验证**：
```
2178468632   修复前 1.0 / 0（全白）→ 修复后 0.2063 / 0.171（参考 0.2051 / 0.174）
daemon 路径  2178468632 / 3003908979 / 3497575235 全部正常，且三帧 md5 互异（在动）
2889253135   仍为 3.5%，未被波及
3808922316   其 effect 同样引用 _rt_ 目标，现在改为保留基础图像（内容变化属预期）
回归测试     tests/data/scene/rtfallback + test_scene_gl：撤掉修复变红，加回变绿
```

**教训**：`REA-COMPARISON` 里"本侧已实现"是按"解析+调用路径存在"标的，不等于行为正确。
另外，本次排查中我曾因**改动后忘记重新链接捕获工具**而得出"旧版本正常/新版本正常"的
错误结论，浪费了大量时间——测试台与二进制必须同时重建，且任何结论都要在
"同一二进制、多次运行"下成立。
