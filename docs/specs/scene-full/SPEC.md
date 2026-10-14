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
- **回归修复（2026-10-13）**：上一条把采样器默认记入 `defs`（kind 0）后，`bind_prog_defaults` 的 switch
  没有 case 0，落到 `default:` 分支对**采样器 uniform**调用 `glUniform4f` —— 非法操作且会把采样器绑定的纹理
  单元改写成 0，导致纹理全乱（3003908979 的窗框 `genericimage2` 直接消失，用户反馈为"前后景分离"）。
  现在 kind 0 直接跳过（采样器默认走纹理槽绑定路径）。窗框已恢复。
- **2178468632 无雨滴（2026-10-13 已修）**：雨滴用的是 `genericparticle` + `REFRACT=1`，该分支执行
  `color.rgb *= texSample2D(g_Texture3, ...)`，而 `g_Texture3` 的默认是 `_rt_FullFrameBuffer`（渲染目标、
  非文件）→ 我们跳过 → 采样未绑定纹理得黑色 → 颜色乘 0 → 雨滴不可见。现已在粒子路径对每个空槽解析
  注释默认，`_rt_FullFrameBuffer` 用"当前已累积场景"的窗口后台缓冲快照作为纹理源；雨滴重新可见。
  随后又发现两处根因并修复（见下一条）。
- **粒子着色器路径的投影 + spritetrail（2026-10-13 已修）**：2178468632 的雨此前不可见有两处根因：
  ① **投影**：着色器路径把粒子顶点（引擎像素、居中空间，发射盒 ±1200）配**单位 MVP**直接上传，
  所有粒子都落在裁剪空间外被丢弃，只有原点附近的幸存且以 NDC 尺度绘制（这就是早期"白方块"的来源）；
  点精灵路径是在 CPU 转 NDC 的，所以不受影响。现在粒子绘制绑定由目标尺寸推出的**正交投影**。
  ② **spritetrail**：引擎把该渲染器实现在 `genericparticle` 内、由 `TRAILRENDERER` combo 驱动——
  顶点着色器用 thick 格式（`a_TexCoordVec4C1`）里的速度 + 拖尾长度推导带状四边形。现在解析渲染器的
  `length`/`maxlength`/`minlength`，对这些粒子定义 `TRAILRENDERER` + `THICKFORMAT`，并喂
  `g_RenderVar0`（长度）、`g_RenderVar1`（精灵宽高比）与 common_particles.h 需要的正交相机参数。
  效果：2178468632 出现下落雨丝；3003908979 / 3497575235 无回归（雨滴四边形尺寸也变正确）。
- **壁纸属性（project.json）未生效（2026-10-13 已修）**：这是"特效感觉不对"的主因。对象的
  `{"user": name, "value": fallback}` 覆盖项此前一律取 **value**（内联回退值），从不查 project.json 里
  声明的属性值。2178468632 的雨因此速度用了 1（属性 `rain_speed` 实为 **0.5**）、寿命用了 0.39
  （`rain_intensity` 实为 1）→ 雨又快又短，尺寸/密度也与预览不符。现在解析完 project.json 后统一把
  `{user}` 解析为属性值（属性缺省时才用回退值），并对粒子的 count/lifetime/size/speed 生效
  （此前 count/lifetime/size 解析了但**从未使用**）。另引入 `XW_DYN_UNSET`：零初始化的覆盖项表示
  "无覆盖"（中性值 1.0），不再被当成"常量 0"（那会让池大小/寿命/尺寸清零）。
- **顺带修**：粒子渲染里 `nframes` 在精灵上传**之前**取样，恒为 0，导致 `randomframe` 永远选第 0 帧。
- **精灵图集选帧（2026-10-13 已实现）**：`particle/drizzle` 是 1024×512 的**图集**（8 帧 256²，每帧是一团
  柔和的白斑；雨丝是靠 TRAILRENDERER 沿速度拉长出来的）。此前每个四边形把**整张 8 帧图集**压进一个小方块
  → 正是用户说的"形状抽象"。现在按引擎的做法选帧：`SPRITESHEET` + `g_RenderVar1`（帧宽高/张数/宽高比），
  帧矩形**已是归一化 UV**（缓存给出的是 0.25/0.5 这类值，我一度误当成像素又除了一次 → `frac(frame*0.0002)`
  → 每帧都采样图集角上的透明区 → 雨几乎不可见；已修正为直接使用）。
  仅对**真图集**启用（帧小于整图）；逐帧独立贴图的序列（如 3003908979 的雨滴）不走该路径——否则会退化成
  白方块（已实测）。同时对齐 `CParticle::setupPass`：粒子**恒**定义 `THICKFORMAT`（我们的顶点布局就是 thick
  格式），且槽 0 **永不**被 shader 的 `util/white` 默认覆盖（引擎把槽 0 绑到粒子自身的贴图）。
- **已知差异**：雨的颗粒度/密度与官方预览仍有差距（逐粒子贴图选择、REFRACT 帧缓冲贡献、alpha 未完全对齐）。
- **`.pkg` 场景读不到 project.json（2026-10-14 已修）**：`scene.pkg` 会被解包到临时目录再从那里解析，而
  `project.json` 在 **.pkg 旁边**、不在包内 → 属性永远为空（`n_props=0`）→ 所有 `{user, value}` 覆盖都退回内联值。
  2178468632 因此 `rain_speed` 用 1（作者设 0.5）、`rain_intensity` 用 0.39（设 1）、最明显的是
  `size_changer` 用 2（设 1）→ 雨滴**大一倍**。现在解包时把同目录的 project.json 一并带入；斑点的 size 由
  15/21/20 变为 7.5/10.7/10。
- **A/B 基准（重要工具）**：参考实现自带截图，可直接当 ground truth：
  `xvfb-run -a -s "-screen 0 1920x1080x24" linux-wallpaperengine --assets-dir <assets> --window 0x0x1920x1080
   --screenshot /tmp/ref.png --screenshot-delay 60 --silent <壁纸目录>`，再与我们的 `shot` 同尺寸逐像素比。
  2178468632 当前差距 **0.0278**（修复前 0.0486）。**教训**：之前用整帧 RMSE 判断"渲染了没有"是错的
  （混入帧时序噪声）；必须**看放大截图** + 用参考截图做基准。
- **已知差距**：斑点（speckles）在天空形成细密"细胞/砂纹"观感，而参考是更细的线状雨；逐粒子贴图选择、
  REFRACT 帧缓冲贡献与 alpha 仍未完全对齐。
- **脚本化向量属性（2026-10-14 已修）**：对象的 `origin`/`scale`/`size` 除了 `"x y z"` 字符串与数组形式，还有
  **脚本属性**写法 `{"script":…, "scriptproperties":…, "value":"x y z"}`（引擎每帧跑脚本驱动它，例如用时间或
  滑块）。此前 `get_vec2/3/4` 只认前两种 → 整个场景直接解析失败（用户实测 2794098047 报
  "object origin/scale/size must be [x, y]"）。现在三个读取器都先解包该 dict 取其基准 `value`；
  **逐属性脚本本身尚未求值**（记入已知差距），基准值至少让对象落在作者放置的位置。
- **雨的量化差距（2026-10-14 测量，未修）**：用参考实现的截图做基准，把"粒子贡献"单独隔离出来
  （各自与关闭粒子/`--disable-particles` 的同一帧相减）：
  - 参考：max 53199 / **mean 35.6** / sd 727
  - 我们：max 65535 / **mean 351.5** / sd 1472  → **总强度约 10×**
  形状：参考是**细长竖直雨丝**（约 2px 宽、15~40px 长），我们是一片**圆斑**。
  已确认的事实：
  ① `rain sprite` 与 `drizzle` 都是 **4×2 图集、每帧一块实心白斑**（我解出贴图看过）——所以"圆斑"对这两张贴图
     是**忠实**的；参考之所以看着是雨丝，是因为其**拖尾（spritetrail）粒子占主导**。
  ② 我们的 downpour（TRAILRENDERER）粒子**几乎不可见**：单独保留它时天上近乎空白；而把斑点跳过才有细痕。
     它们是被**后绘制的斑点覆盖**（斑点是 alpha=1 的实心白斑），或自身贡献过小。
  ③ `REFRACT` **是生效的**（关掉它贡献立刻 ×8，2830）：所以雨滴确实被帧缓冲采样压暗了。
  ④ 组合/参数都正确：`TRAILRENDERER=1`、`SPRITESHEET=1`、`rv0=(5,30,0)`、`rv1=(0.25,0.5,8,0.5)`；
     强制把 clamp 从 30 改到 300 **画面无变化** → 拖尾几何没有体现出来（待查：属性绑定/顶点数据/绘制顺序）。
  下一步候选：粒子对象间的绘制顺序与叠加（斑点把拖尾盖掉）、斑点的 alpha/不透明度、以及 WE 的
  `translucent` 是否为预乘（我们现在是 SRC_ALPHA/ONE_MINUS_SRC_ALPHA）。
  **更正（同日，加入确定性渲染后复查）**：上面的对照**混入了 1.17% 的帧间噪声**（同样设置连跑两次的
  整帧差异），所以我当时"拖尾不可见 / 雨强 10×"的结论**不可信**。加入 `XW_FIXED_TIME`（用帧计数驱动时钟，
  两次渲染逐位相同）后重测：**拖尾贡献 mean 17.5、斑点 mean 58.5**，且把拖尾单独渲染放大对照，
  **形状与参考几乎一致（都是细长竖丝）**。结论修正为：拖尾**没问题**，多出来的是**斑点**——
  我们的斑点在画面里成片、且直径约 15~20px，而参考里只有零星几颗约 5~6px。下一步应查斑点的
  尺寸/不透明度（`sizerandom 15 / 2 * size_mult(1)` 应给 7.5 半径）与其 `REFRACT` 结果。
- **flags & 4 粒子的透视投影（2026-10-14 已实现）**：引擎对 `flags` 带 bit2 的粒子另用一套相机
  （`perspective(fov, aspect, near, far) * lookAt((0,0,1000)→原点, up=+Y)`，eye=(0,0,1000)），
  其余粒子用场景相机的正交投影。现已解析 `general.fov/nearz/farz`（默认 50/0.01/10000）与粒子的 `flags`，
  并在粒子绘制时按该位选择投影（`xw_gl_particle_perspective()` + 绘制函数的 mvp 覆盖）。实测确认生效：
  2178468632 的 `Rain_downfall_light/heavy`（flags=7）走透视分支、斑点（flags=0）走正交。
  **但这一帧观感仍未对齐**：当前可见的雨以正交斑点（圆斑）为主，拖尾粒子只贡献很淡的调制；把 fov 从 50
  强改到 8 画面几乎不变 → 说明这些粒子本身在图上的存在感极低，与"是否透视"无关，仍需继续定位
  （候选：斑点后画不透明覆盖、斑点强度、以及拖尾几何未光栅化）。
- **确定性渲染开关 `XW_FIXED_TIME`（2026-10-14）**：置位后场景时钟改用帧计数（`frames / fps`）而不是
  wall clock，dt 固定为 `1/fps` → 同一命令连跑两次**逐位相同**（RMSE 0）。**所有"贡献度/差异"类测量
  必须在此模式下做**：默认时钟下两次渲染相差 1.17%，与要测的效应同量级（我此前多轮结论因此失准）。
- **"抽象形状"的真凶：`_rt_imageLayerComposite_*`（2026-10-14 已修）**：2178468632 的云层是靠**引擎管理的
  渲染目标**合成的——`clouds 1/2` 本身 `visible:false`（渲染进 `_rt_imageLayerComposite_<id>_<x>`），
  可见的 `Compose clouds 1/2` 再把这些 RT 与基础层混合（`BLENDMODE` 2/7 + `multiply`）。**我们不产出这些 RT**，
  于是那些 pass 采样到"纹理单元上残留的东西"，在整个天空画出一层**斑驳的"裂纹/细胞"纹理**——这正是
  用户说的"雨滴和蒸气的形状很奇怪"。修法（保守降级）：pass 声明了无法解析的 `_rt_*` 纹理时**保留底图**
  而不是画垃圾。效果：天空恢复成与参考一致的干净渐变，对参考基准 **0.0277 → 0.0226**。
  **教训**：这是**图像层**的问题，不在粒子上——我之前几轮都在粒子路径里找，方向错了。
- **帧缓冲快照改为每帧一次（2026-10-14）**：引擎的 `_rt_FullFrameBuffer` 是**每帧一份**资源；我们此前是
  **每个对象各拍一次**，于是后画的对象采到的是"背景 + 前面已画的粒子"，把各 emitter 的效果逐层叠加。
  现在 `xw_gl_frame_begin()` 每帧只标记一次、快照在同一帧内复用 → 与引擎一致，并把每帧 5 次全屏拷贝降为 1 次
  （顺带解决之前记录的 performance 项）。对参考基准 0.02262 → 0.02254。
- **当前差距归因（2026-10-14）**：**基础层（关粒子）已与参考几乎完全一致**（均值 0.2023 vs 0.2024，
  标准差 0.1636 vs 0.1640；RMSE 1.2% 基本是参考自身截图时刻的差异）。**剩余差距全在粒子**：
  我们的斑点比参考更"多而明显"，而拖尾（细丝）已一致。参考的斑点几乎不可见（其 `_rt_FullFrameBuffer`
  采样时机可能让雨滴≈背景），这是下一步要钉的点。
- **`sphererandom` 发射器（2026-10-14 已实现）**：语料统计里 **12/22 个 emitter 是 `sphererandom`**，它与
  `boxrandom` 有两处**数据形态差异**，此前都被丢掉：① 它的 `distancemin/distancemax` 是**标量数字**
  （如 `1000`），不是向量字符串 → 我们只认字符串 → **半径恒为 0**；② 我们没有 emitter 名字分支 →
  半径 0 的"盒"让所有粒子**堆在发射点一个点上**。现在：`get_dist3()` 同时接受标量与向量字符串，
  解析 `name` 得到球/盒标记，并解析 `sign`（每轴强制正负）。模拟侧按 `CParticle::createSphereEmitter`
  实现分布——**正交粒子填 2D 圆盘**（按面积均匀：`sqrt(rand(minR²,maxR²))`），**透视粒子（`flags&4`）
  填 3D 球壳**（按体积均匀：`cbrt(rand(minR³,maxR³))`）再投影到 XY；之后按 `directions` 缩放、
  按 `sign` 强制符号。实测 2889253135（雾/雪/落叶）、3808922316（星空，`flags=4`）、2794098047
  （透视雪）三张真包出图正常且粒子铺开（修复前挤在一点）。
  **顺带更正**：`directions` 缺省 `(1,1,1)` **早已实现**（`scene_parse.c` 的粒子初始化处）——
  我之前在数据调查中把它判为缺失是**误报**。
  **仍缺**：球发射器的 `speedmin/speedmax`（向外初速度；语料中仅 3808922316 使用且值为 1）。
- **振荡器 operator（2026-10-14 已实现）**：语料里 6 个粒子系统使用 `oscillateposition`(9) / `oscillatealpha`(5)，
  此前只有 `movement`/`alphafade` → 这些系统**失去摇摆与闪烁**。语义（照 `CParticle::createOscillate*Operator`）：
  每个粒子**只抽一次** frequency/scale/phase（phase 跨度 `phaseMin..phaseMax+2π`），之后按 age 驱动——
  `oscillateposition` 逐轴积分 `-scale*w*sin(w*age+phase)*dt`（引擎的 `w = 2π·f/2π` **化简为 f**）再乘 `mask`；
  `oscillatealpha`/`oscillatesize` 用 `mix(scaleMin, scaleMax, (cos(w·age+phase)+1)/2)` 缩放**基准值**。
  `alphafade` 末尾**同步振荡器的 alpha 基准**（参考同款做法）→ 两者是**串接**而非互相覆盖。
  缺省照 `ObjectParser::parseOperator`：position freq 0..5 / scale 0..10 / mask (1,1,0)；alpha 0..10 / 0..1；
  size 0.8..1.2。`oscillatesize` 语料中 0 次但同族，一并实现。
  **仍缺**：`angularmovement`(4)、`angularvelocityrandom`(4)、`controlpointattract`(4)（下一轮）。
- **粒子配置缺省（2026-10-14 已修，重要）**：**30 个粒子配置里 8 个没有 `maxcount`、3 个 emitter 没有 `rate`**，
  我们两者都缺省成 **0** → **池为 0 → 一颗都不发**：2889253135 的 dust motes/bokeh、3422426571 的雪
  **整层缺失**。引擎缺省为 `rate` **10**（`parseParticleEmitter`）、`maxcount` **100**、
  `animationmode` **"sequence"**（`parseParticle`）。修后真机对比 2889253135 出现此前完全没有的 bokeh 光斑
  （对上一版整帧 RMSE 0.0297）。另确认：`starttime` 参考**解析但从未使用**（我们同样忽略，属正确）。
- **球发射器 `speedmin/speedmax`（未做）**：参考的球发射器会额外把初速度设为"从球心向外"方向 ×
  `rand(speedmin,speedmax)`（盒发射器不做）；语料中仅 3808922316 使用且 min=max=1，影响很小，记入待做。
