# Spec: scene-full-support（WE Scene 壁纸全面支持）

## Objective

xwallpaper 在裸 X11 上全面对标 linux-wallpaperengine 的 scene 壁纸能力。
用户有一份 Wallpaper Engine scene 壁纸目录（`project.json` + `scene.pkg`），
执行 `xwallpaper set <dir>` 即可得到与参考实现视觉/行为一致的动态壁纸，
覆盖 image/effect、particle、text、sound、user properties、audio 联动、
mouse/parallax 输入与 JS 脚本驱动。

**User stories:**
- 作为 X11/WM 用户，我 `xwallpaper set ~/wallpapers/3497575235` 得到与
  linux-wallpaperengine 一致的 image+effect 动画壁纸。
- 粒子壁纸的 emitter/sprite 正常喷发、排序、混合，不崩溃。
- 文本壁纸用 FreeType 渲染字形，内容正确。
- 含音频的 scene 正常循环播放，可 `--mute`/`--volume` 控制。
- 含脚本的 scene 按 timeline 驱动对象属性；`--list-properties`/`--set-property` 可覆写用户属性。
- 鼠标移动产生视差/输入响应（可 `--disable-mouse`/`--disable-parallax` 关闭）。
- DPMS 关屏暂停、恢复继续；`clear`/`--list`/`exit` 语义与其他后端一致；重启 daemon 可恢复 scene。

**非目标（首版不做）：** 3D 模型对象（WE 有但 linux-wallpaperengine 亦部分支持）、
视频壁纸复用（已有 video 后端）、Wayland layer-shell 上的 scene 首发（X11 先行，
wl.c 复用既有窗口抽象后补）。

## Tech Stack

- C11，desktop GL 3.3（复用 x11 窗口，不另开 X 连接），json-c（parse），
  imlib2（PNG/JPG 解码复用），freetype2（text），SDL_mixer 或 mpv 复用二选一（sound，
  spec 阶段定案），quickjs 最小嵌入集 vendored（scripting，无外部包依赖）。
- Build: `make` + `pkg-config`；CLI 对象与 daemon 对象分离不变。
- Test: 纯 C assert harness（无外部框架），沿用 `tests/test_*.c` + `tests/integration/`。

ASSUMPTIONS I'M MAKING:
1. 对标基线 = linux-wallpaperengine main（2026-10），不追踪其历史版本行为。
2. 渲染用 desktop GL 3.3 直渲 X window（沿用历史 scene 方案验证过的路线），不用 EGL surfaceless。
3. sound 后端默认 SDL_mixer（与 mpv 视频隔离），若评审倾向复用 mpv 则在 plan 阶段切换。
4. quickjs 以 vendor 源码编译进 daemon，不引入系统包依赖。
5. Wayland scene 后补，首版只验收 X11（含 Xvfb 集成测试 + 真机冒烟）。
→ 如有误现在纠正，否则按此推进。

## Commands

```
Build:      make
Run tests:  make test
Integration: make test-xvfb
Check deps: make check-client-libs   # CLI 必须仍只链 libc 系
Clean:      make clean
Debug:      make DEBUG=1
Manual smoke:
  ./xwallpaperd &
  ./xwallpaper set <scene-dir>                  # 目录含 project.json + scene.pkg
  ./xwallpaper --list
  ./xwallpaper --list-properties -m DP-1        # 用户属性（live override）
  ./xwallpaper --set-property rain=0 -m DP-1     # 覆写后即时生效，不落盘
  ./xwallpaper clear
  ./xwallpaper exit
```

## Project Structure

```
src/
  scene_pkg.c/.h      → PKGV 解析（scene-pkg）
  scene_tex.c/.h      → TEXV 解码（scene-tex）
  scene_shader.c/.h   → WE GLSL 转译（scene-shader）
  scene_parse.c/.h    → 全对象模型（scene-parse）
  scene_model.h       → 对象/DynamicValue/camera/property 结构
  scene_core.c/.h     → FBO/camera/quad/blend/present/bloom（render-core）
  scene_image.c/.h    → image+effect（scene-image）
  scene_particle.c/.h → 粒子（scene-particle）
  scene_text.c/.h     → 文本（scene-text）
  scene_sound.c/.h    → 音频对象（scene-sound）
  scene_script.c/.h   → quickjs 引擎（scene-script）
  scene.c/.h          → XWBackend 壳（scene-backend）
  vendor/quickjs/     → quickjs 最小嵌入集（core+unicode/regexp/bignum）
tests/
  test_scene_pkg.c / test_scene_tex.c / test_scene_shader.c / test_scene_parse.c
  test_scene_core.c（纯逻辑：FBO 表/camera/blend/combo 推断）
  integration/test_scene_*.c（xvfb，有 GL 则渲、无则 skip）
  data/scene/         → 合成小 fixture（脚本生成）+ 真实 json 文本（小尺寸入库，大 pkg 不入库）
docs/specs/scene-full/
  CAPABILITY.md       → 模块表（本目录）
  SPEC.md             → 本文件
  SPEC-<module>.md    → 各模块 spec（plan 前逐个补齐）
```

## Code Style

```c
/* scene_parse.h — 只读对象模型，arena 分配 */
int xw_scene_parse(const char *dir, XWScene *out, XWError *err);
void xw_scene_free(XWScene *s);

/* scene_core.h — render-core 原语，唯一 GL owner */
int xw_core_present(XWCore *c);  /* scene FBO → window */
```

- 命名：`xw_<module>_<action>`；类型 `XW*`；与现有 `xw_cli_*`/`xw_app_*` 一致。
- C11，无注释除非解释非 obvious 的 WE 语义复刻点（注明对标文件如 `CImage.cpp:271`）。
- 错误：`XWError{code,msg[256]}`，返回 int（0=ok）；坏输入拒绝不崩溃。
- 后端绝不另开 X 连接，复用 `x11` 的 `XWWindow*`；GL 单线程在 daemon tick 渲染。

## Testing Strategy

- **Unit（无 X）：** pkg/tex/shader/parse/core 纯逻辑，assert harness，`make test` 全绿；
  合成 fixture（脚本生成小 pkg/TEXV）+ 真实小 json 文本入库；15MB 级真实 pkg 不入库，仅手动冒烟。
- **Integration（Xvfb）：** 每渲染模块一个集成测试，无 GL 时 skip、有 GL 时断言无 GL error；
  `make test-xvfb` 全绿。
- **Manual（真机）：** 3 类代表壁纸（image-effects / particle / text+sound+script）冒烟；
  与 linux-wallpaperengine 参考二进制 `--window --screenshot` 同帧对比（静态区误差 ≤3/通道）。
- Coverage：纯逻辑模块 ~90%；GL/脚本行为靠集成+真机对比。

## Boundaries

- Always: `make test` 先行；CLI 薄约束（`make check-client-libs` 绿）；坏包/坏 json 拒绝不崩溃；
  所有 X/GL/音频资源 destroy 释放；沿用 TDD + 增量实现。
- Ask first: 新增系统依赖；改 `backend.h`/`daemon` 主循环；改 x11 窗口 visual；
  Wayland scene 接入；sound 选 SDL_mixer 还是复用 mpv。
- Never: CLI 链 json-c/freetype/GL/quickjs；后端另开 Display；fork 外部播放器；
  提交 secrets/大 pkg 二进制；为过测试删断言。

## Success Criteria

- `xwallpaper set <scene-dir>` 对 image/particle/text/sound/script 各至少一款代表壁纸可跑，
  与参考二进制同帧对比达标（静态区 ≤3/通道，动效区在阈值内）。
- `--list` 显示 `type=scene`；`clear`/`exit`/重启恢复语义与其他后端一致；scene↔image 切换无泄漏。
- `--mute`/`--volume` 对 scene 音频生效；`--disable-mouse`/`--disable-parallax` 生效；
  用户属性可列出、可覆写（CLI 形态在 plan 定案后冻结）。
- `make test` / `make test-xvfb` / `make check-client-libs` 全绿。
- 无 fork 外部播放器；CLI 启动仍 <1ms 量级（无重型库）。

## Open Questions（已决议，见 Decisions）

1. sound 用 SDL_mixer 新增依赖，还是复用现有 mpv？→ **SDL_mixer**（daemon 专属 chunk 循环，与 mpv 视频隔离）。
2. 用户属性 CLI 形态 → **`xwallpaper --list-properties [-m]` / `--set-property name=value [-m]`**（live override，不落盘；daemon 精确 verb 匹配）。
3. 粒子/脚本代表壁纸选哪几个 Workshop ID 做验收？→ **未取真实壁纸**，以合成 fixture（`tests/data/scene/`）+ xvfb 像素回读验收；真机冒烟待用户提供目录。（2026-10-13 补充：用户提供 2178468632/3497575235 后，已用其 scene.pkg 在 xvfb 下联调出图；回写见 Decisions effect 运行时。）
4. Wayland scene 是否纳入首版验收？→ **不纳入**（X11 先行；scene 后端 `init_wl` 为 NULL，daemon 明确报错）。
5. quickjs vendor 路径与升级策略 → **`vendor/quickjs/` 直接 copy 上游 2024-01-13 最小嵌入集**（core+unicode/regexp/bignum，MIT LICENSE 保留；升级时整目录替换）。

## Decisions（实现中落定的补充决议）

- **渲染走 GLX compat 上下文**（非 core profile）：WE 方言 shader（`gl_FragColor`/`texture2D`/`varying`）经转译后可编译；Mesa/NVIDIA 均支持。
- **Uniform 绑定按名回退**：sampler 取 `xw_tex0`→`g_Texture0`→`tex0`→首个 sampler；time 取 `xw_time`→`g_Time`；alpha 统一走合成 pass 的 `xw_alpha`。
- **失败策略**：解析/脚本/纹理缺失 fail fast（`set` 直接报错）；帧内渲染失败回落 clear 色（壁纸活着）；音频缺设备/坏文件降级 mute。
- **effect 运行时（2026-10-13 真包联调补）**：material shader 名省略 `shaders/` 前缀（resolve 时回退补齐）；effect 用同名 `.vert`（无则内置 quad），`a_Position→0`/`a_TexCoord→1` 绑定；`#include` 恒注入 prelude（texSample2D/mul/saturate/frac/lerp/CAST2-4/M_PI 系列/rotateVec2/ApplyBlending≈mix）+ `// [COMBO]` 默认 `#define`；material combos 进程序缓存键；draw 时绑 `g_Texture0..7`（0=输入，缺槽回退输入）+ `g_Texture{i}Resolution=(texW,texH,realW,realH)` + 单位 MVP + `g_Time`/`g_TexelSize` + 注释 default 被 scene 常量（按 material 名）覆盖；`util/black|white|noflow|perlin_256` 程序化；单 pass 失败降级到已画结果（不断整对象）；TEXB0004 非视频按 0003+isVideo 解析（视频拒绝），内嵌 PNG 不校验 usize。两张真实 Workshop 包（2178468632/3497575235）xvfb 像素回读已出图且与 preview 色系一致。
- **effect 后续**：render-target/`bind: previous|_rt_*`（glitter 等多 pass 效果当前只跑出单链近似）、material 文件 combos 与 scene combos 合并、音频频谱 uniform（恒 0）、`util/` 更多内置。
- **reload 数组计数（2026-10-13 panic 修复）**：emitter/text/sound 数组的释放一律用分配时记录的 `n_emitters/n_texts/n_sounds`，不再用当前 `scene.n_objects`——reload 先换 scene 后 `init_emitters`，对象数变大时旧 `free_emitters` 越界读并 `free` 野指针（`free(): invalid pointer`，2178468632/3003908979 切换必现）。回归测试 `test_scene_reload_grow`（2→13→4→13→2）旧码必崩。
- **合成顺序**：image → text → particle 的 per-type 循环；object-order 与 scene-space FBO 合成是后续任务。
- **粒子排序**：未做（swap-remove 打乱顺序；当前 blend 下可接受，记录为后续项）。
- **文本**：Latin-1 单行；UTF-8/CJK shaping 后续。
- **props 不持久化**：override 只活在 daemon 内存，重启/切换丢失（后续可加 state 列）。
- **`--disable-mouse`/`--disable-parallax`**：未实现（输入恒为 root 像素：脚本可用；decorrelation 开关后续）。
- **真机同帧对比**：未执行（无参考壁纸目录）；xvfb 像素回读 + 动画推进断言代替。

## 渲染重写（2026-10-13，分支 `refactor/scene-render-pipeline`）

背景：原 shader 处理是手写 token 替换，实机多张 Workshop 壁纸失败。用户要求对标本地
`linux-wallpaperengine`。许可冲突已决议：**保持 MIT**，只照其"行为/流程"重写，不搬 GPL 代码。

- **许可**：参考实现是 GPL-3.0，本项目是 MIT。逐文件照搬会强制本项目 copyleft，故**不照搬**。
  采用的 `glslang`（BSD/Apache）与 `spirv-cross`（Apache-2.0）是宽松许可，可安全使用/依赖。
- **shader 管线路线（照参考 `ShaderUnit` + `GLSLContext` 的行为）**：WE 方言预处理 →
  glslang 编译为 SPIR-V → spirv-cross 回译为 GLSL 330。参考用自建 fork；我们用系统库。
- **M1（已完成）**：`scene_shader.c` 重写为真正的预处理器——`#version 330` 头 + 方言 `#define`
  （mul/lerp/frac/saturate/CASTn/texSample2D/…）+ 按阶段映射（frag：`varying`→`in` +
  `out_FragColor`；vert：`attribute`→`in`、`varying`→`out`）+ `#include` 解析（外部 resolver
  优先，回退内建头，缺失降级为注释）+ `// [COMBO]`→`#define`（material 显式值优先）。
  内建 `common.h`/`common_fragment.h`/`common_vertex.h`/`common_blending.h` 按 WE 官方文档重写
  （https://docs.wallpaperengine.io/en/scene/shader/headers.html ）。
- **修正实锤 bug**：官方文档 `M_PI_2 = 6.28318530718`（=2π）、`M_PI_HALF` 才是 π/2；旧实现把
  `M_PI_2` 当 π/2，且缺 `M_PI_HALF/SQRT_2/SQRT_3` 与 `hsv2rgb/rgb2hsv/greyscale`。
- **M2（待做）**：接入 glslang→SPIR-V→spirv-cross，替换对驱动 GLSL 编译器的直接依赖。
  前置：`sudo pacman -S spirv-cross`（glslang 已装）。随后继续按参考补 effect render
  target/`bind: previous|_rt_*`（`CPass`/`CFBO` 行为）与粒子系统。
- **未验证项**：内建头里 `DecompressNormal*`/`ConvertTexture0Format`/`BuildTangentSpace` 等
  函数体官方只公布签名未公布公式，按标准实现，需真机比对。
- **M1.5（已完成）**：`linked varying` 兼容——WE 自家 shader 对会出现 vert 声明 `varying vec2 X`、
  frag 声明 `varying vec4 X`（`glitter_prepare` 就是）；预处理时把 vert 声明就地加宽并把裸赋值
  补成 `vec4(expr, 0.0, 1.0)`。3497575235 的 effect 编译从 9/10 提升到 10/10。
- **render target 降级（已完成）**：解析 pass 的 `target`（`_rt_*`）；因尚未支持离屏路由，命中
  target 的 pass 让**整个 effect 降级**（保留上一良好结果），避免把离屏 pass 的输出混进主链
  （否则 3497575235 会被 glitter_prepare 冲成黑屏）。真离屏支持（CFBO/`bind: previous`）留待 M3。
- **渲染三连修（2026-10-13，3497575235 实机）**：用户反馈"诡异动画/左右大幅重叠"。抓到三个真 bug：
  1. **FBO 往返翻转**：quad 的 uv 约定是 v=0 在顶，而 GL 的 FBO 原点在左下，导致每过一次 FBO 就翻一次，
     朝向取决于 pass 奇偶。改为渲染进 FBO 时对 Y 取反（`g_ModelViewProjectionMatrix` 用 flip 矩阵，
     内建 vert 用 `xw_flip` uniform），FBO 纹理与源纹理朝向一致，任意 pass 数都正立。
  2. **pass 纹理路径基错**：scene 文档里的 pass 纹理引用（`masks/xxx`）相对 **`materials/`**，我按场景根
     解析 → 全部遮罩加载失败 → `g_Texture1` 回退成输入图 → 全图被 mask 扭曲成拖影。改为 materials 优先、
     场景根回退（合成 fixture 用场景相对路径）。粒子 sprite 同理。
  3. **wrap 标志被忽略**：对 effect 采样强制 `GL_REPEAT`，而主纹理带 `ClampUVs`(flags=2) → 位移效果把画面
     环回出边缘窄条。改为按纹理 flags 决定 clamp/repeat。
  另外：支持 per-effect `visible`（349 的 effect 6 是 false）；补 `texture2D`→`texture` 方言宏（老 shader）；
  纹理缓存上限 8→24（349 需 1 底图 + 9 遮罩）。
- **雨声残留（非代码 bug）**：用户 state 里 `screen` 输出仍挂着 2178468632（雨声壁纸），且其 1920x1080 与
  DP-1 的 2560x1440+1080 在 x∈[1080,1920] 重叠——"重叠+雨声"来自另一个输出的残留壁纸。清除：`xwallpaper clear -m screen`。
- **M2（已完成，2026-10-13）**：接入 glslang → SPIR-V → SPIRV-Cross（照 `GLSLContext::toGlsl` 的流程）：
  `src/scene_glsl.c` 用 glslang C API（`GLSLANG_SHADER_AUTO_MAP_BINDINGS|AUTO_MAP_LOCATIONS`）+ spirv-cross
  C API（`spvc_*`，GLSL 330）。两库均为宽松许可（BSD / Apache-2.0），MIT 干净。Makefile 自动探测，
  缺库时降级为"预处理源码直接交给驱动"。
  两个要点：① 编译为 SPIR-V 要求显式 location，故预处理阶段给 `a_Position`/`a_TexCoord` 钉死
  `layout(location=0/1)`（否则 glslang 自动映射与我们的 VAO 槽位不符）；② uniform 的
  `// {"material":...}` 注释必须在回译**前**收集，SPIRV-Cross 输出会丢注释。
  `glslang_initialize_process()` 成功返回非 0（转发 `ShInitialize` 的 bool）——一开始判断反了，
  导致静默回退；`tests/test_scene_glsl.c` 覆盖可用性/往返/坏 shader 报错。
- **M3（已完成，2026-10-13）**：effect 离屏 render target / bind，按参考 `CImage::setupPasses` +
  `configurePassTarget` 的行为实现：解析 effect 的 `fbos`（name/size/format/uvs）与 per-pass `bind`
  （`{name,index}`）；GL 侧新增命名 target 表（`xw_gl_named_target`/`xw_gl_bind_named`，`r8`→GL_R8，
  `repeat`→REPEAT）；pass 驱动照参考：命中 `target` 的 pass 写入命名 FBO 并进入 "target 序列"，
  `bind` 里 `previous` 解析为序列入口纹理、其余名字解析为命名 FBO，非 target pass 走 ping-pong 并结束序列。
  `tests/data/scene/effecttarget` 覆盖 fbos/bind/target 解析。
- **combo 语义（2026-10-13）**：补齐 WE 的"采样器 combo"规则——`uniform sampler2D g_TextureN; // {"combo":"X"}`
  仅在该槽**实际绑定纹理**时把 X 定义为 1（官方文档 `variables.html#optional-visible-texture` 明示）。
  这条不实现会让 `#if MASK` 恒假（遮罩失效、混合量恒为 1）。
- **ApplyBlending（2026-10-13）**：按标准图层混合模式表实现（Normal/Darken/Multiply/Screen/Overlay/
  Additive/Difference/Exclusion/Subtract/Divide/Grain 等），未知/越界模式回退 additive。WE 未公开
  `imageblending` 的数值枚举，属近似；3497575235 的 glitter（BLENDMODE=32）靠此回退得到正确的"镜片闪光"。
- **M4 第一片（2026-10-13）**：发射器按参考 `CParticle::createBoxEmitter` 重写——
  位置在**中心化像素空间（Y 向上）**，emitter `origin` 翻 Y，在 `distancemin/max` 盒内随机生成并按
  `directions`（Y 翻转）缩放；速度取 `velocityrandom` 的 min/max **向量**并翻 Y（旧实现只取模长、
  方向丢失、固定在 (0,0) 生成 → 粒子瞬移出屏）；`movement` 的 `gravity` 翻 Y、支持 `drag`。
  另修：真实壁纸走 `particle` 配置路径，粒子颜色被 memset 成 0 → alpha 恒 0（不可见），已在对象解析处
  统一默认不透明白。
  渲染：新增条纹路径（`xw_gl_program_streaks`/`xw_gl_draw_streaks`，沿速度拉成细条），`renderer: spritetrail`
  用它；2178468632 的雨现在真的可见且在动。
- **M4 待做**：`renderer: sprite` + `animationmode: sequence` 的**精灵图逐帧采样**（未实现前直接跳过，
  否则整张 sheet 会被画成大方块——这是本轮刻意保留的保守降级）；`rope`/`ropetrail` 渲染器、
  控制点、振荡器（oscillateAlpha/Size/Position）、湍流、音频驱动发射、子粒子系统、`fadeAlpha/fadeSize`。
- **M4 第二片（2026-10-13）**：精灵图逐帧采样。逐帧信息来自**纹理**的 `TEXS` 动画块（不是粒子配置）——
  `rain sprite.tex`/`drizzle.tex` 都是 TEXS0003：1024x512 里 8 帧 256x256。实现：`xw_tex_decode` 扫
  `TEXS0001/0002/0003` 解析帧矩形与时长（`XWTexImage.frames/n_frames/frame_time`）→ 纹理缓存保存归一化
  帧矩形 → `xw_gl_texture_uv_rect(tex, time)` 按时间取当前帧 → 点精灵/条纹着色器用 `xw_uv_rect` 采样子矩形。
  动画时钟 = 发射器累计时间 × `sequencemultiplier`。2178468632 的雨点由"整张 sheet 大方块"变为逐帧圆点。
- **M4 待做**：`fadeSize`/`fadeAlpha` 曲线、每帧独立时长（当前按帧数均分）、`rope`/`ropetrail`、
  控制点、振荡器、湍流、音频驱动发射、子粒子系统。
- **M4 第三片（2026-10-13）**：按参考补齐粒子生命周期语义——
  ① 逐帧时长按各帧 `frametime` 累加（不再均分）；
  ② 帧索引由**粒子 age** 驱动（`fmod(age*sequencemultiplier, sheetDuration)`，参考 `CParticle` 的做法），
     改为每粒子一个 frame 索引（`XWPoint.frame`/`XWStreak.frame` + 着色器 `uniform vec4 xw_frames[16]`）；
  ③ `alphafade` 算子（`fadeintime`/`fadeouttime` 在生命周期位置内淡入淡出），并去掉我原先自造的
     "线性淡出"——参考在没有 alphafade 时 alpha 恒定；
  ④ `sizerandom` 的 `exponent` 曲线与 `/2` 缩放。
  2178468632 的雨点由大圆点变为细小白色雨滴，观感正常。
- **M4 待做**：`rope`/`ropetrail`、`randomframe`/`once` 动画模式、控制点、振荡器、湍流、
  音频驱动发射、子粒子系统、`fadeAlpha`/`fadeSize`（参考里是 renderer 字段，仅 rope 用）。
- **M4 第四片（2026-10-13）**：按真壁纸实际用到的初始化器补齐（先统计了三张壁纸的 operator/initializer
  用量：`rotationrandom`×3、`alpharandom`×2、`randomframe`×2、`colorrandom`×1、`turbulentvelocityrandom`×1）：
  ① `alpharandom`/`colorrandom`/`rotationrandom` 初值；② 每粒子旋转（`XWPoint.rot` + 片元里绕
  gl_PointCoord 中心旋转）；③ `animationmode: randomframe`（每粒子稳定随机取帧，用 spawn 时保存的 `rnd`）。
  另修一个真 bug：内嵌 PNG 的 TEXV（FIF=13）走**提前 return**，绕过了 TEXS 解析 → 该纹理帧数为 0、整张
  sheet 被当作一帧采样（3003908979 的雨滴贴图正是这种）。
- **M4 已知缺口（重要）**：粒子的**材质 shader** 没有实现——我们只把 sprite 当贴图做点精灵，而 WE 会跑
  粒子材质自己的 pass（如 `rainrefractive` 的折射、`halo_50` 用 blank sprite + 法线做玻璃感）。
  因此 3003908979 的雨滴显示为白色方块（预览里是微弱折射）。要修需实现粒子材质的 pass 渲染
  （实例化四边形 + 粒子 shader），属较大一块。
- **WE 资源目录（2026-10-13）**：`genericparticle` 等**引擎内建 shader 不在 pkg 里**（和 `common.h` 一样），
  参考实现也依赖用户的 WE 安装。本机 `steamapps/common/wallpaper_engine/` 只有 UI/config，**没有 assets/**。
  已加 `XW_ASSETS_DIR` 环境变量：设置后 `#include` 解析与内建 shader 查找都优先用
  `<assets>/shaders/...`（用真的 `common.h`/`common_blending.h` 替掉我们的重写版），未设置则回退到现有行为。
  `tests/test_scene_shader.c` 用合成 assets 目录覆盖（覆盖内建头 + 解析 `genericparticle` + 复位回退）。
- **待做**：拿到 WE 资源后，实现**粒子材质 pass 渲染**（参考的 17 float/顶点布局：`a_Position`/
  `a_TexCoordVec4`/`a_Color`/`a_TexCoordVec4C1`/`a_TexCoordC2`），才能跑真 `genericparticle` 的折射粒子。
- **拿到 WE 真实资源（2026-10-13）**：用户跑过 WE 后资源解出，路径
  `~/.local/share/Steam/steamapps/common/wallpaper_engine/assets`（另有 `/mnt/windows_d/...` 一份）。
  用 `XW_ASSETS_DIR` 指向它即可让 `#include` 与内建 shader 走真文件。核对结果：
  ① 真 `common.h` 的常量与按文档实现的完全一致（`M_PI_2 = 6.28318530718` 等）——验证了之前的修法；
  ② 真 `common_blending.h` 的 `ApplyBlending` 是 **编译期 `#if BLENDMODE == N`** 链，编号为
     1=Darken、2=Multiply、4=Subtract、7=Screen、9=Add、…、31=`A+B*opacity`、**32=`mix(A, A+A*B, opacity)`**、
     其余回退 Normal。**我原先按 reshade 猜的编号是错的**，且 32 不是"替换"而是"保留 A 再叠加 A*B"
     ——这正是 349 glitter 之前镜片发黑的原因。回退版已按真编号重写（与真资源差异 1.7% → 0.83%）。
  ③ `genericparticle.vert` 用的属性与参考 17 float/顶点的布局完全对应
     （`a_Position`/`a_TexCoordVec4`/`a_Color`/`a_TexCoordVec4C1`/`a_TexCoordC2`）。
- **下一步**：实现**粒子材质 pass 渲染**（属性绑定 + 17 float 顶点流 + 绑定 sprite/法线/combos/常量），
  跑真 `genericparticle`，修掉 3003908979 的白色方块（应为折射雨滴）。
- **粒子材质 pass 脚手架（2026-10-13，未激活）**：已解析粒子材质的完整 pass（shader/textures/combos/
  constants/blending），并按参考的 **17 float/顶点**布局（`a_Position`/`a_TexCoordVec4`/`a_Color`/
  `a_TexCoordVec4C1`/`a_TexCoordC2`）加了粒子四边形绘制路径与属性钉位；当材质 shader 能编译时启用，
  否则安全回退到现有 sprite/spritetrail 渲染。
  **阻塞**：真 `genericparticle.vert` 经我们的方言转译后编译失败——glslang 报
  `too many parameters to 'vec4' constructor`，但按其给出的行号（0:170）定位到的行并无该调用，
  怀疑与 `#define in_ParticleRotation vec3(...)` 之类的宏展开或行号映射有关，尚未定位。
  解决后即可让 3003908979 这类折射粒子显示正常。
- **粒子材质 pass 已激活（2026-10-13）**：定位并修掉两个阻塞点后，真 `genericparticle` 通过。
  ① `xw_shader_fix_linked_varyings` 原先不理会 `#if`，把 frag 里**非激活分支**的 `varying vec4`也当成
  真实声明，于是把本已匹配的一对（`genericparticle` 在 `#if SPRITESHEET` 下分别声明 vec2/vec4）加宽，
  链接反而失败；现在按行维护 `#if` 栈并用 combo 定义求值（标识符/`defined()`/`!`/`&&`/`||`/括号/字面量），
  且**只对真正加宽过的声明**做 `NAME = vec4(expr, 0.0, 1.0)` 填充——之前会把 `v_Color = a_Color;`
  （`a_Color` 本身是 vec4）改成 6 分量构造而编译失败。
  ② pass 生效后：3003908979 的折射雨滴正常渲染（不再是白色方块）；3497575235 / 2178468632
  逐 pass 验证全部编译通过（含 `glitter_prepare`）。
  **前提**：需要 `XW_ASSETS_DIR` 指向引擎 assets（内建粒子 shader 不在 pkg 内）；未设置时仍走简化渲染器。

## Spec: WE assets 目录自动探测（2026-10-13）

**目标**：不设 `XW_ASSETS_DIR` 时，也能自动找到 Wallpaper Engine 的 assets（内建 shader 所在），
使粒子材质 pass 等能力开箱可用。

**契约**（`src/scene_shader.h`）
- `int xw_shader_resolve_assets_dir(const char *env, const char *home, char *out, size_t n);`
  优先级：`env`（即 `XW_ASSETS_DIR`）非空 → 直接采用；否则在 `home` 下探测。命中返回 0 并写入 `out`。

**探测规则**
- 有效候选：`<候选>/shaders/common.h` 可读。
- `$HOME` 下的固定 Steam 根：`~/.local/share/Steam`、`~/.steam/steam`、`~/.steam/root`、
  `~/.var/app/com.valvesoftware.Steam/.local/share/Steam`（Flatpak）、
  `~/.snap/steam/common/.local/share/Steam`（Snap）。
- 每个根的 `steamapps/libraryfolders.vdf` 里 `"path"` 指向的其它库（同时兼容旧式
  `"N" "<路径>"`），追加 `<库>/steamapps/common/wallpaper_engine/assets`。
- 候选只读探测；找不到 → 返回 -1，调用方保持原行为（简化渲染器）。

**边界**
- Always：只读探测、无分配、找不到不报错（可降级）。
- Never：不联网、不写用户目录、不加新依赖；CLI 不受影响（只在 daemon 侧 scene init 调用）。
- **已实现**：`xw_shader_resolve_assets_dir(env, home, out, n)` —— `XW_ASSETS_DIR` 优先，否则探测
  `$HOME` 下 5 个固定 Steam 根 + 各根 `steamapps/libraryfolders.vdf` 列出的其它库（兼容新旧两种写法），
  命中判定 `<候选>/shaders/common.h` 可读；`src/scene.c` 在 scene init 调用，`XW_DEBUG_DAEMON` 下打印命中路径。
  单测覆盖 env 优先 / 默认库 / vdf 第二库 / 未命中；真机不设 env 也能自动激活粒子材质 pass。
- **turbulentvelocityrandom 已实现（2026-10-13）**：按真实用量统计选中（11 个 scene pkg 的全部 json：
  2 个壁纸有实际参数；oscillators/ropetrail 0 次，children 全 null，controlpoint offset 全 0，
  ledsource 全 false —— 这些先记为"数据侧无实际影响"）。实现：在生成位置采样 curl noise（含每粒子
  phase 与时间平移），`scale < 2` 限制偏离 `forward` 的夹角、`offset` 绕 `right` 倾斜、2D 粒子丢弃 z 漂移；
  forward/right 做 Y 翻转贴合屏幕坐标。**噪声场为引擎内部实现、未公开**，此处用标准 Perlin 改进噪声 +
  中心差分 curl，置换表由固定种子生成（未复制任何参考表）。
- **采样器注释默认纹理（2026-10-13）**：效果 shader 的采样器常带 `// {"default":"util/perlin_256"}` 之类
  注释（引擎全量统计：38 个采样器有字符串默认，其中 **0 个**同时带 `"combo"`），材质没给纹理时就该用这个默认。
  我们此前完全不读它 → 该采样器未绑定 → 采样到纹理单元上的残留。glitter_prepare 即此例：它把 smoothstep
  跑在白噪声上而不是引擎的平滑 Perlin 噪声。现在 `xw_shader_collect_defaults` 收集采样器默认（kind 0 + 纹理 ref），
  编译后的 program 暴露之，effect 绘制时用程序默认填补材质留空的槽（`_rt_*`/`_alias_*` 交给 pass bind 处理）。
  另：`util/<name>` 优先加载引擎自带文件 `<assets>/materials/util/<name>.tex`（perlin_256 为平滑噪声），
  无 assets 时回退到合成纹理。
- **3497575235 眼镜"灰块"结论（2026-10-13）**：用户报告眼镜特效疑似异常。经比对——从 pkg 解出角色原图
  （TEXV 内嵌 PNG，3924×2208）缩放到 1920×1080 后与我们的渲染逐像素对照，透镜上的灰色圆角块**原画里就有**
  （画师的镜面高光/反光），不是渲染缺陷；`блеск очки`（glasses shine，即 glitter 效果）能编译、能动画
  （`_rt_GlitterTiles` 在 t≈0 与 t≈30 差异 29%），把它的 alpha 归零后画面仅差 1.7%（细微光泽），属正常。
  真正修掉的是上面那条"采样器默认纹理未绑定"。
