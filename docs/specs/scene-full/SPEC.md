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
- **角运动 operator + angularvelocityrandom（2026-10-14 已实现）**：语料 4 个系统用
  `angularvelocityrandom`(4) + `angularmovement`(4)。语义（照 `CParticle::createAngular*`）：
  `angularvelocityrandom` 每轴 `min + pow(rand, exponent)*(max-min)` 再乘速度倍率；
  `angularmovement` 先 `rotation += angularVelocity*dt*speed`，再叠加 `force`、按 `drag` 衰减，
  最后把角度**绕回 (-π, π]**。引擎是 vec3，我们的四边形只承载面内自转 → 只取 **Z 分量**。
  缺省照 `ObjectParser`：`±5 rad/s`、drag 0、force 0。
  **真机 A/B 结论（重要，避免误判）**：对 2887099508 关/开自转，整帧 RMSE **0**——不是 bug：
  该壁纸画布 **6080×3420**，受影响的 Sakura 对象在 `x=4159`（**在我们视野之外**，因为相机
  `eye/center` lookAt 尚未实现），另一处 `light_shafts` 的 `rate` 仅 **0.2/s**（3 秒内 0~1 颗）。
  即语料中受该特性影响的系统本身几乎不可见，价值在于**忠实度**而非观感。
- **真实 text 对象格式（2026-10-14 已修，重要）**：2887099508 **整包加载失败**，根因是我们此前**自造**了
  text 的数据形状。真实格式：内容就在 `text` 本身（**字符串**，或 `{"script":…, "value":…}` 的脚本串），
  `font` / `pointsize` / `color` 是它的**同级字段**；字号在 **`pointsize`**（不是 `size`，后者是对象几何），
  颜色是 **RGB 三元组**（可 `{user,value}`）。现按真实格式解析；fixture 同步改成真实形状。
- **animated / scripted 数值兜底（2026-10-14 已修）**：`alpha` 除了数字与 `{user}`/`{expression}`，还有
  `{"animation":…, "value":…}`（关键帧）与 `{"script":…, "value":…}` 两种——2887099508 里 **17 个对象**
  用前者。现 `parse_dynfloat` 对**任何带数值 `value` 的 dict** 取基准值（`user` 非字符串时不再误当引用）。
  修后该壁纸从"整包拒绝"变为**正常出图**。
- **相机 eye/lookAt 实为无操作（2026-10-14 证伪，重要）**：曾计划"实现相机 `eye`/`center` lookAt"以修正
  大画布构图，**动手前先验证前提**发现：全语料 9 张壁纸的 `center - eye` **恒为 `(0,0,-1)`**、`up=+Y`
  → `lookAt = T(-eye)`，而正交投影里已 `glm::translate(projection, eye)` → 两者**正好抵消** →
  **eye/lookAt 对正交相机是净无操作**（参考源码里那句 TODO "THROWS OFF POINTS" 也印证了这点）。
  **结论：不做。** 教训：**先验证前提再实现**，否则会白做一整个特性。
- **两处对报告的更正**：① 报告称"自动投影尺寸（isAuto）🔴未实现" —— 实际 `xw_camera_auto_size()`
  **早已实现**（与参考 `CScene` 的 isAuto 逻辑一致）；② 报告称"eye/center lookAt 🔴" —— 见上，实为无操作。
- **拖尾宽高比 `g_RenderVar1.w`（2026-10-14 已修）**：该值喂给 `common_particles.h` 的 `textureRatio`，
  **缩放拖尾的长度轴**。引擎算的是**每帧**宽高比 `(atlasH×frameH)/(atlasW×frameW)`（无 sheet 时才用
  `realH/realW`）；我们此前用了**图集**的 `h/w` → `drizzle`（1024×512、4×2 方形帧）得到 **0.5 而非 1.0**
  → **每条雨丝短了一半**（"短横划线"而非"细长竖丝"）。修后真机确认：拖尾恢复为**细长竖丝**，与参考形状一致。
  **数值注意**：整帧对参考的 RMSE 由 0.0225 → **0.0271**（变差）——因为拖尾变长覆盖更多像素，而
  **斑点才是数值主导项**（斑点贡献 58.5 vs 拖尾 17.5）且仍然过亮过密。**这是下一个目标，不是本次修复的缺陷。**
  公式已单测（scene_core，含 sheet 与非 sheet 两种情形）。
- **斑点可见度的精确量化（2026-10-14 测量，未修）**：用确定性渲染做**贡献隔离**（各自与"关粒子/跳过拖尾"的同帧相减）：
  - 我们的**拖尾贡献 0.000516** ≈ 参考的**粒子总贡献 0.00054** → **拖尾量级正确** ✓（也验证了 `rv1.w` 修复的方向）
  - 我们的**斑点贡献 0.00175** → **超出量几乎全部来自斑点**（总计 0.00227 vs 参考 0.00054 ≈ **4.2×**）
  - 天空背景两边**完全一致**（0.4301 vs 0.4302）✓；单颗统计接近（max 1.29×、sd 1.22×）但**均值 2.9×** →
    差异在**覆盖面积/数量**，不是单颗亮度。
  **已逐项核对引擎源码、确认我们与之一致**（因此都不是原因）：`sizerandom` 确实 `/2`；
  `lifetimerandom` 是**普通均匀抽样**（引擎连 `exponent` 都不读）；单颗颜色链 = 精灵色 × 场景色(g_Texture3) × overbright
  (1.25)；`count`/`lifetime` 覆盖 = `rain_intensity` = **1**（已核对 project.json）；`Rain_downfall_heavy` 的
  `maxcount` 确实是 **5**、light 是 1000（我们的池与之相符 ✓）；帧 40 时斑点各存活 2733（仍在爬升，稳态被
  maxcount 10000 截断），与引擎同量级 ✓。
  **剩余可疑点（下一步）**：斑点四边形的**绘制覆盖**本身——`ComputeSpriteFrame` 的帧选择/帧间混合
  (`v_TexCoordBlend`)、`orientation: "screen"`（我们尚未解析）、以及 REFRACT 的采样偏移
  (`refract_amount 0.4` + 法线图) 是否让参考的斑点被"抹平"。
  **参考侧 FFB 时机**：其 `_rt_FullFrameBuffer` 由**合成 bloom 图**写回，且 REFRACT 另拷贝一份影子 FBO 防反馈
  （`CParticle.cpp:1673`），与我们的"每帧一次快照"不完全等价——留作后续对照项。
- **alphafade 缺省值（2026-10-14 已修）**：引擎 `ObjectParser` 对 `alphafade` 的缺省是
  `fadeintime 0.5` / `fadeouttime 0.5`，我们此前是 `0.0 / 1.0` ✗ → **从不淡出**。斑点配置正是
  `{"fadeintime": 0, "name": "alphafade"}`（**只写 fadeintime**）→ 参考的斑点在**后半生淡出到 0**，
  我们全程 alpha=1 ✗。`fadeValue` 已核对为**线性 lerp**（与我们一致 ✓）。
  **注意**：在对比时刻（约 1.3–2s）粒子寿命 17.5s、`lf≈0.08` **尚未进入淡出** → 该修复**不改变该帧** ✓，
  影响的是**稳态**（壁纸长时间运行时）。
- **`orientation` 无需实现（2026-10-14 核实）**：参考**同样不解析** renderer 的 `orientation`（其
  `g_OrientationUp/Right/Forward` 是固定默认值）→ 我们忽略它与参考一致 ✓，不是差异来源。
  `ComputeSpriteFrame` 用**寿命分数** `floor(frac(lifetime)*numFrames)` 选帧并做**帧间混合**；
  我们传的是量化后的 `frame/nframes`（能选中同一帧、但混合恒为 0）——记为已知小差异。
- **对比方法学问题（2026-10-14 发现，重要）**：`--screenshot-delay` 的单位是**帧**（非秒），
  两边帧率未知；且粒子池是**爬升**的（帧 40 时斑点各 2733，稳态被 maxcount 10000 截断 → 稳态 2 万）。
  实测：帧 40 贡献 0.00227，**12s 时升到 0.00796**。**结论：斑点可见度的对比必须在两边相同的
  场景时间、且处于稳态下进行**，否则计数不可比。此前"斑点 3.2×"的结论需在此前提下重测。
- **稳态对比证据（2026-10-14，可复用）**：参考侧用 `--screenshot-delay 600`（单位是**帧**）跑出稳态截图
  `/tmp/opencode/ref_ss.png` ✓；我们渲染 180 帧（≈6s，已达池上限）得 `steady_ours.ppm`。
  结果：**参考是细长雨丝（约 12 条/裁剪区）+ 极少小点**，**我们仍是大量圆斑铺满 + 仅约 3 条丝** ✗ →
  **"斑点过可见"在稳态下同样成立，不是时间假象** ✓（此前的 3.2× 结论成立）。整帧 RMSE 0.0371。
- **剩余唯一可疑机制（下一步）**：精灵表的**帧 UV 映射**。引擎的 `ComputeSpriteFrame` 产出
  `(curCol, curRow, nextCol, nextRow)` + `frameBlend = frac(lifetime*numFrames)`，顶点着色器把它写进
  `v_TexCoord`（vec4）后 `v_TexCoord += uvFrameSize.xyxy`；片元以 `v_TexCoord.xy` 采样并做**帧间混合**。
  我们传的是**量化后的 `frame/nframes`**（能选中同一帧，但**混合恒为 0**），且我们自己的
  `xw_frames[16]` 简化路径与此不同。需逐行核对这条链，确认采样区域是否比引擎更大（覆盖面积差异的来源）。
- **帧 UV 映射链核对完毕（2026-10-14）：我们是对的 ✓，排除**。引擎 `genericparticle.vert` 里
  `v_TexCoord.xy = a_TexCoordVec4.xy` 后调 `ComputeSpriteFrame(frac(in_ParticleLifeTime), v_TexCoord, ...)`
  → `v_TexCoord` 被写成 `(curCol, curRow, nextCol, nextRow)`，再 `uvOffsets *= step(0.001, corner)`、
  `v_TexCoord += uvOffsets.xyxy` → **四角正好映射到帧矩形** ✓。片元的帧间混合由 `SPRITESHEETBLEND` 控制，
  该材质（halo_*）**未定义**该 combo → 混合恒 0 ✓，与我们一致 ✓。`v_ScreenCoord = gl_Position.xyw` ✓
  （自动计算 ✓，REFRACT 采样坐标正确 ✓）。
- **overbright 代码路径（2026-10-14，已澄清；我先前的结论有误，此处更正）**：我先做了一次"把
  `g_Overbright` 常量改成 1.0"的实验，贡献**完全不变**（0.00853289 两次一致），据此**误判**为"材质常量
  不经该路径生效"。复查代码后发现：`bind_prog_defaults()` 是按**材质名**匹配的（shader 注释里的
  `"material":"ui_editor_properties_overbright"` ↔ 材质 JSON 的键同名），**而我实验时用的键是
  `g_Overbright`（uniform 名）→ 自然匹配不上** ✗。**结论：常量本来就被正确应用 ✓**（`g_Overbright` 确实是 1.25 ✓），
  我们的斑点 = 背景×1.25 ✓ **与材质一致** ✓。因此"参考的斑点几乎不可见"是**参考侧自身管线**的行为
  （最可能与其 `_rt_FullFrameBuffer` 的写入时机/影子 FBO 有关，见上文），**不是我们的缺陷**。
  **取舍**：忠实于 WE 的材质取值（1.25）✓ 还是模仿参考的观感 ✗ —— 目前选择**忠实** ✓。
- **依赖序渲染（2026-10-14 已实现）**：原先按**类型**分三趟（image → particle → text）✗，现在按引擎的
  `dependencies` **DFS 后序**单趟绘制（`xw_scene_render_order()`：先依赖后自身，跳过自依赖/未知 id/重复；
  场景顺序为并列次序），由 `scene.c` 的 `render_overlays()` 按序分派 image/particle/text。
  顺带把图像渲染拆出**单对象**入口 `xw_scene_image_draw()`（`xw_scene_image_render()` 保留为批处理壳，
  集成测试仍在用）。渲染顺序在解析后**构建一次**（`build_order()`，init/reload 各一次），每帧复用。
  **实测**：2178468632 逐位**无变化** ✓（该场景本就"图像在前、粒子在后"，与 DFS 结果一致）；
  3808922316 / 3497575235 / 3003908979 三张真包出图正常、无回归 ✓。
- **文本脚本层（2026-10-14 已实现）**：文本对象的 `text` 若是 `{"script":…, "value":…}`，引擎每帧调用脚本的
  `update(value)` 并用**返回值**作为该层的文本（时钟/计数器的做法）。现已在 `scene_text_gl.c` 接入：
  解析时把内联脚本存进 `XWTextCfg.script`（堆分配，`xw_scene_free` 释放），绘制时按需建
  `XWTextLayer` 并每帧 `update`；**重新光栅化时保留 layer**（脚本状态不丢 ✗ 修正为 ✓ 保留），
  仅后端销毁时释放。**真机验证**：2794098047 出现 **`12:59`** 与 **`14 Oct,2026`**（真实当前时间/日期 ✓，
  含 `scriptProperties.delimiter` 与月份数组 ✓）——此前只有静态占位文本 ✗。
- **新发现：文本的定位不走画布→窗口映射（2026-10-14，未修）**：`xw_scene_draw` 直接用
  `(int)origin[0], (int)origin[1]` 当**窗口像素**绘制 ✗，而图像是走正交投影（画布→窗口）✓。对
  3440×1440 这类画布，原点语义应为中心化画布坐标 ✗ → 文本会落在错误位置（实测落在左上角 ✗）。
  记录为下一步项（与图像的定位逻辑对齐）。
- **文本定位（2026-10-14 已修）**：文本的 `origin` 是**中心化画布坐标（Y 向上）**，而此前直接当**窗口像素**用 ✗
  → 在 3440×1440 画布上落到左上角。现在新增纯逻辑 `xw_text_place()`（画布→窗口映射 + 字形缩放 =
  画布纵向比例 ✓），`xw_scene_text_draw()` 多收一个 `XWSceneCamera*`（取正交画布尺寸），按
  **锚点=文本中心** 摆放，并按 `size × scale` 光栅化。**真机验证**：2794098047 的三个脚本化文本层
  （`14 Oct,2026` / `13:08` / `2025/10/14 13:08:24`）现在**居中在画布上** ✓（此前在左上角 ✗）。
  集成测试 `test_scene_text` 同步更新（传 camera、按新映射断言像素区域）。
  **顺带发现**：图像的绘制是**全屏拉伸**（不读 `origin`/`size` ✗）——因为语料的底图多为画布尺寸所以看起来正常，
  但严格说对象的摆放尚未实现，记为后续项。
- **文本锚点是画布像素（2026-10-14 更正上一轮）**：上一轮我按"中心化坐标"实现文本映射 ✗。查参考
  `CText.cpp` 的源码注释后确认：**文本的 origin 是画布坐标（Y 向下、原点左上）**，映射为
  `gl_y = origin.y - scene_h/2`（**与 CImage 的翻转不同**，注释里明确写了 ✗ 这一点）。
  已更正 `xw_text_place()`：**直接按画布比例缩放到窗口**（不居中、不翻转 ✓），字形按画布纵向比例光栅化 ✓。
  单测更新为真实数据（2887099508 的时钟在画布 (5476,475)/(6080×3420) → 窗口 (1729,150) ✓）。
- **对象摆放缺失是"可见"级别的差距（2026-10-14 实测）**：用参考渲染 2887099508 得到**完全不同的构图** ✗
  （参考是招牌/logo 区域，我们不是）——根因是**我们的图像是全屏拉伸**（不读 `origin`/`size` ✗），
  而参考按**画布矩形**摆放（`CImage`：`size` 缺省取纹理真实尺寸、`size×scale`、origin 居中、
  再翻转到画布 Y 向下 ✓）。这也解释了为什么修正后的文本仍看不到 ✗：**后续的全屏图像把它盖住了** ✓。
  这是下一步的主要项（同时会影响所有壁纸的构图，风险较高，需要逐张对比）。
  另：参考在 **2794098047 上会自身崩溃** ✗（`nlohmann type_error: type must be number, but is string`），
  无法作为该壁纸的基准。
- **对象矩形的约定已查明（2026-10-14），但摆放暂未落地（有可见回归，已回退）**：
  参考 `CImage` 的矩形数学 = `size`（缺省回退**纹理真实尺寸**）× `scale`，`origin` 是**矩形中心**且为
  **画布像素**：**X 从左往右** ✓、**Y 从画布底部往上** ✓（CImage 之后才翻转到 Y 向下的场景空间）。
  用真实数据核对无误：2887099508 的 `Solid` origin `(3240,1710)`（= 画布中心 ✓）size 1×1 × scale 6480×3420；
  `cloud` origin `(3136.98,2174.06)` size 4982×1733 —— **只有"Y 自下而上"才能让它落在画布上方** ✓。
  （对照：**文本的锚点是画布 X + Y 向下** ✗ 与图像不同，见 `CText` 的源码注释 ✓。）
  **实测（接线后）**：构图**明显更接近参考** ✓（同一布局 ✓），但仍有**逐对象误差** ✗ —— 人物偏移、
  出现一个悬空精灵 ✗ → 整帧 RMSE **变差**（2178468632 0.0271→0.075；2887099508 0.0347→0.239 ✗）。
  按"**不允许可见回归**"原则**已回退** ✓（纯逻辑矩形函数与其单测一并移除，避免死代码）。
  **下一步需要**：逐对象定位误差来源 —— 首要怀疑**脚本化/动画化的 origin**（我们只取基准值 ✓，
  2887099508 大量对象如此）与 alignment/parallax ✓。
- **对象矩形的 Y 约定已用实测钉死（2026-10-14）**：`cy = canvas_h - origin.y`（X 为 `origin.x` ✓）。
  **判据**：取 2887099508 最大的两个对象（`new background1` 6480×3420 与 `ldfk` 6080×3420，origin.y 均为
  1710 = 画布垂直中心 ✓）——按此约定它们的矩形恰好是 `top=0, h=窗口高` ✓ **正好铺满窗口**；
  而**照字面**读参考 `CImage` 的 `scene_h/2 - origin.y` 会得到 `top=-540` ✗（**连整画布背景都被推出画面** ✗，
  不可能 ✓）。→ 说明参考在 parser 里对 origin 做过空间转换（很可能已取反 ✓），**不能照字面照搬** ✗。
- **脚本化 origin 不是误差源（2026-10-14 已排除）**：2887099508 有 **18 个对象**是脚本化 origin，
  但其基准值**都是合理的画布坐标** ✓（如 `6855 1626`、`-3130 2565` —— 后者是"设置"菜单从画面外滑入的
  隐藏位置 ✓），不是占位符 ✗ → 先前"脚本属性未求值导致错位"的假设**不成立** ✗。
- **仍未定位**：接线后残余的**逐对象**误差（人物偏移、一个悬空精灵 ✗）。已排除：Y 约定 ✗、脚本 origin ✗。
  下一步应做**单对象**验证工装（逐对象渲染并与参考同区域比对），而不是整帧 RMSE ✗ —— 整帧会被少数错位对象主导 ✓。
- **文本光栅化：UTF-8 + 多行（2026-10-14 已修）**：语料 **30 个文本对象里 11 个内容是 CJK** ✓
  （如 `博士不会又在想什么涩涩的事吧？` ✓，还有多行 `\n` ✓），而此前光栅化器**按单字节**
  `FT_Load_Char(*s)` 处理 ✗（UTF-8 多字节 → 乱码 ✓）且**单行** ✗（`\n` 被当普通字形 ✗）。
  现在：新增纯函数 `xw_utf8_next()`（1~4 字节解码 ✓，畸形首字节跳过 1 字节保证前进 ✓）；
  光栅化改为**按码点** + **按行**（`\n` 换行、行高取字体 metrics ✓），宽度取最宽一行 ✓。
  单测：UTF-8 解码（ASCII/2/3 字节/空串/畸形 ✓）、多行高度 ✓、CJK 真实字体出墨 ✓（6/6 ✓）。
  **验证限制（如实）**：这些 CJK 对象的基准位置**在画面外** ✗（如 `待解锁功能` 基准 x=7105 →
  窗口 2244 ✗，脚本在菜单打开时才把它移入 ✓）→ **默认状态下无法目视验证** ✓，本轮以单测为准 ✓。
- **`controlpointattract` operator（2026-10-14 已实现）**：语料 4 处使用（如 `dust_motes_0`：
  `controlpoint 1 / origin "0 0 0" / scale -1024 / threshold 32` ✓，**负 scale = 排斥** ✓）。
  语义（照 `CParticle::createControlPointAttractOperator`）：**先 `threshold /= 2`** ✓，
  `center = controlPoints[cp].position + origin` ✓，对 `0.001 < distance < threshold` 的粒子施加
  **径向力** `direction * scale * dt * speed` ✓。控制点表按 `controlpoint[]` 的 `id` 落位
  （引擎把表 resize 到 8 ✓），缺省照 `ObjectParser`：controlpoint 0 / origin (0,0,0) / scale 100 / threshold 1000 ✓。
  **至此语料中实际用到的 operator 已全部实现** ✓（movement / alphafade / oscillateposition / oscillatealpha /
  oscillatesize / angularmovement / controlpointattract；其余在语料中 0 次 ✓）。
- **剩余缺口的数据排查（2026-10-14，用于选下一步）**：
  - **`camerafade`（8 张壁纸）**：参考里**只解析给脚本用** ✓（`WallpaperParser` 存 `.fade`，
    仅 `thisScene.camerafade` 暴露给 JS ✓），**渲染路径完全未使用** ✗ → **忽略它是正确的** ✓，无需实现 ✓。
  - **`animationlayers`（45 个对象 / 3 张）**：**宿主 100% 是 puppet（3D 骨骼模型）** ✗，非 puppet **0 个** ✓；
    层里的 `animation` 指向的是骨骼动画片段（"呼吸/动作/眨眼" ✓）→ 属**3D 模型大特性** ✗，
    与报告里的 puppet 缺口同源 ✓，**不是合适的下一项** ✗。
  - **`fullscreen` 对象：语料中 0 个** ✓ → 报告里的"fullscreen 图层"一行实为**无实际影响** ✓。
  - **文本排版是剩余可见项** ✓：对齐分布为 `center/center` 18 个 ✓、**`h=left` 8 个** ✗（我们会画成居中 ✗）、
    `maxwidth` 30 个 ✗、`padding` 29 个 ✗ → 下一步做**文本水平对齐 + padding + maxwidth 折行** ✓。
- **文本排版无需实现（2026-10-14 核实）**：参考 `CText.cpp:234` 明确写着
  `// multi-line wrapping, alignment, and padding come with Phase 2.` → **参考自己也没实现** ✗✓。
  因此对齐（8 个 `h=left`）/`padding`(29)/`maxwidth`(30) **不是与基准的差距** ✓，实现反而会**偏离基准** ✗。
  同类的还有 `camerafade`（参考只给脚本 ✓）。**教训：动手前先确认"参考是否做了"，否则会做超出基准的工作。**
- **收尾核查（2026-10-14）**：语料 **9 张壁纸全部渲染成功**（0 失败 ✓），抽查三张画面正常 ✓。
- **脚本化对象属性：运行器（2026-10-14 已实现，接线待做）**：语料有 **70 个脚本化属性**
  （`origin`/`scale`/`size`/`alpha` ✓；**61 个混淆** ✗ / 9 个可读 ✓）。可读的那个极其简单
  （2794098047 `秒数.scale`：`value.x = new Date().getSeconds()/60` ✓）→ 只需**可变的 Vec3** ✓ + 标准 JS ✓。
  已实现 `xw_script_prop_create/update/free` ✓：前置环境给 `Vec3` ✓、`WEMath.mix/clamp` ✓、
  `createScriptProperties()` ✓（与文本层共用 ✓）、`shared`/`console` ✓；`update(value)` 收/回一个对象 ✓。
  **安全降级**：脚本**抛异常**（如引用尚未建模的 `thisScene` ✗）→ 返回 -1 ✓，**调用方保留基准值** ✓
  —— 这正是 2887099508 那 18 个混淆 origin 的期望行为（菜单静止时应在画面外 ✓）。
  单测用**真实语料脚本**验证 ✓（`秒数.scale` → `out.x ∈ [0,1)` ✓）+ `WEMath.mix` ✓ + 抛异常降级 ✓ + 坏脚本拒收 ✓。
  **待做（下一片）**：解析时保留属性脚本源 ✓、每帧求值并写回对象 ✓。
- **属性求值接线为何暂缓（2026-10-14，数据结论）**：`xw_script_prop_*` 已实现且有单测 ✓，但**接线暂无可见收益** ✗，
  故**保留为未接线的地基**（本身不影响渲染 ✓，等价于"特性开关默认关" ✓）。依据：
  - 70 个脚本化属性中 **61 个混淆** ✗，依赖 `thisScene.getLayer(...)` ✗ → 需整套**图层注册表** ✓；
    按现有降级策略它们**保持基准值** ✓（静止状态下本就正确 ✓）。
  - 9 个可读的里：**2 个滑块驱动的常量 origin** ✓（无动画 ✗）、**1 个图像 scale** ✓、**1 个滑块驱动的文本 origin** ✓
    → 都**不产生可见动画** ✗；唯一有动画的（`秒数.scale` ✓）驱动的是**图像 scale** ✗，而我们的图像路径
    **不使用** scale ✗（全屏拉伸 ✗）。
  - 按对象类型：image 16（origin 10/scale 1/alpha 5 ✓）、text 21（origin 11/scale 4/alpha 6 ✓）、sound 1 ✓。
    其中我们**真正在用**的只有 **alpha**（两条路径 ✓）与 **text origin** ✓；而这两类里可读脚本数为 **0** ✗。
  **解锁条件**：先做**对象摆放**（让图像/文本真正按 `origin`/`scale`/`size` 绘制 ✓）——那之后，
  属性求值 + `thisScene` 注册表才会产生可见效果 ✓。
- **对象摆放：干净的 A/B 结论与两个新发现（2026-10-14）**：用 **stash + 双二进制**做了严格 A/B（同一提交，仅摆放开关）：
  | 壁纸 | 无摆放 | 有摆放 |
  |---|---|---|
  | 2887099508（6080×3420 大画布） | 0.3205 | **0.2395** ✓ 更好 |
  | 2178468632（1920×1080） | **0.0271** | 0.0750 ✗ 更差 |
  → **一半受益、一半受损** ✗，按"不允许可见回归"**仍不落地** ✓（`xw_scene_object_rect` 与其接线已回退 ✓）。
  **新发现 1（重要）：2887099508 的人物本体是 5 个 puppet（3D 模型）** ✗ —— `r ear1`/`back leg body`/
  `l ear1`/`hand book`/`front leg` ✓（40 个普通图像 + 5 个 puppet ✓）。所以**上次看到的"人物偏移"不是摆放的错** ✗，
  而是我们把 puppet 当普通图像画的结果 ✗；**该壁纸的 RMSE 因此无法接近 0** ✓（受 puppet 缺口限制 ✓）。
  **新发现 2**：2178468632 在**无摆放**时右侧有一条**白带** ✗（纹理尺寸≠画布时的未覆盖条 ✓），
  摆放**修好了它** ✓；但摆放同时改变了**雨痕** ✗ —— 粒子系统本不该受图像摆放影响 ✗，
  说明**图像绘制路径的改变影响了粒子 REFRACT 采样的帧缓冲** ✓ → **这是下一个要查的线索** ✓
  （很可能是 `draw_rect_full` 与 `draw_full` 在 alpha/blend 状态上的差异 ✓）。
- **对象摆放：第四次尝试后的定论（2026-10-14）** —— **不落地** ✓，但线索已收敛到"参考行为"层面：
  - **我的 `xw_alpha` 假设被证伪** ✗：给基础拷贝加"不设 alpha"的矩形绘制入口后，2178468632 的帧与修复前**逐位相同** ✗
    → copy 着色器里 `c.a * xw_alpha` 的 alpha 通道**不影响可见结果** ✓（我猜错了机制 ✗）。
  - **确定性已验证** ✓：同一二进制连渲两次**逐位相同**（RMSE 0 ✓）→ 观测到的差异是**真实的** ✗，不是噪声 ✓。
  - **差异图定性** ✓：变化是**整幅画面的边缘**（建筑/灯杆/栏杆全被描边 ✓）= **整体平移** ✓，不是雨本身 ✗。
  - **互相关定量** ✓（缩放 1/4 后 3×3 搜索）：
    | 版本 | 最佳对齐 | 残差 |
    |---|---|---|
    | 无摆放 | **dx=0, dy=0** | **10.2** |
    | 有摆放 | dx=+2（≈原图 +8px ✗） | **38.9** ✗ |
    → **参考的行为等同于我们的"全屏拉伸"** ✓（dx=0 对齐 ✓）；摆放引入 ~12px 平移 **且内容亦有差异** ✗
    （`night blue` 的 origin x = **948.114** ✗ = 作者写的 12px 左移 ✓，摆放会应用它、全屏拉伸忽略它 ✓ —— 但参考似乎不应用 ✗）。
  **结论**：摆放对大画布壁纸（2887099508 ✓）是改进 ✓，对 1920×1080 壁纸（2178468632 ✗）是回归 ✗，
  差异根源在"**参考对每个对象实际怎么摆**"这一层 ✗ —— 需要**逐对象与参考同区域比对**的专用工装 ✓
  （本会话已具备的能力：stash 双二进制 A/B ✓、互相关定位 ✓、差异图定性 ✓）。**在此之前不再尝试接线** ✓。
- **剩余缺口的最终筛查（2026-10-14）—— 结论：无"小而确定且可见"的项了** ✓
  | 缺口 | 语料实测 | 判定 |
  |---|---|---|
  | 音频频谱（`g_AudioSpectrum*` 恒 0） | **引用它的 shader/effect = 0 个** ✗ | **完全无影响** ✓ 无需实现 |
  | bloom 渲染 | **2 张**，强度仅 **0.04 / 0.16**（阈值 0.21/0.65） | 收益很小 ✗；且参考用**合成 bloom 图 + 4 pass 链**（`_rt_4/8FrameBuffer`→`_rt_Bloom`→写回 FFB ✓），
    我们缺少"场景合成目标"✗ → 忠实实现属架构级工作 ✗ |
  | 图像 `alignment` | 6 个对象 ✓ | **依赖对象摆放** ✗（摆放未落地 ✓）→ 现在做无意义 |
  | `cameraparallax` | 2 张 ✓ | 同上：靠移动对象实现 ✗ → 依赖摆放 ✗ |
  | 脚本属性求值 / `thisScene` | 70 个属性（61 混淆 ✓） | 同上：靠移动对象生效 ✗ → 依赖摆放 ✗ |
  | 3D puppet（含 45 个 `animationlayers` ✓） | 3 张 ✓ | **很大** ✗（骨骼 + 网格 ✓） |
  | `fullscreen` / `camerafade` / 文本排版 | 0 / 仅脚本 / 参考亦未做 ✓ | 已确认为**无影响或非差距** ✓ |
  **即：剩余项要么"无实际影响"✓，要么被**对象摆放**这一道门槛挡住 ✗，要么是 3D 大特性 ✗。**

## 会话交接说明（2026-10-14）

**已完成并落地（本会话，按时间序）**
- 确定性渲染开关 `XW_FIXED_TIME`（两次渲染逐位相同 ✓）——**所有测量必须在此模式下做** ✓
- `_rt_imageLayerComposite_*` 保守降级（天空"裂纹"消失 ✓，0.0277→0.0226）
- 帧缓冲快照改为**每帧一次**（与引擎一致 ✓，全屏拷贝 5→1 ✓）
- `sphererandom` 发射器（标量半径 ✓ + 圆盘/球壳分布 ✓，12/22 个 emitter）
- 三个振荡器算子 ✓、`angularmovement`+`angularvelocityrandom` ✓、`controlpointattract` ✓
  → **语料中实际用到的 operator 全部实现** ✓
- 粒子配置缺省（`rate 10`/`maxcount 100` ✓ —— 此前 **8+3 处粒子整层不可见** ✓）
- 真实 text 格式 ✓ / animated·scripted 数值兜底 ✓（2887099508 从**整包拒绝**→正常出图 ✓）
- `alphafade` 缺省 0.5/0.5 ✓（此前**从不淡出** ✓）
- 依赖序渲染（`dependencies` DFS 后序 ✓，取代按类型三趟 ✓）
- 文本脚本层（`update(value)` ✓，真机出现真实时钟/日期 ✓）
- 文本锚点按**画布像素** ✓（更正了我自己上一轮的错判 ✓）
- 文本 **UTF-8 码点 + 多行** ✓（30 个文本对象里 11 个是 CJK ✓）
- 脚本属性运行器 ✓（未接线：**被对象摆放阻塞** ✓，见上）

**实测状态**
- 2178468632（雨）：对参考截图 **0.0486 → 0.0225** ✓；**基础层（关粒子）与参考几乎完全一致** ✓
- 语料 **9 张壁纸全部渲染成功** ✓
- 相似度报告：总体 **≈73%** ✓（渲染主链路 80–90% ✓）

**未落地（各有明确理由）**
- **对象摆放**：4 次尝试 ✓，对大画布改进 ✓、对 1920×1080 回归 ✗；已用互相关证明**参考的行为等同于我们的全屏拉伸** ✓
  → 需要**逐对象与参考同区域比对**的专用工装 ✓ 才能继续 ✓
- 3D puppet（含 45 个 `animationlayers` ✓）：大特性 ✗
- bloom：2 张、强度 0.04/0.16 ✓ → 收益小 ✗
- 其余（alignment/parallax/脚本属性/频谱/fullscreen/camerafade/文本排版）：
  **要么无实际影响 ✓，要么被摆放挡住 ✓，要么参考亦未实现 ✓**

**方法论（本会话最大的教训）**
1. **测量必须在确定性模式下做** ✓（否则噪声会主导结论 ✓ —— 本会话前段因此得出过错误结论 ✓）
2. **动手前先验证前提** ✓（相机 eye/lookAt、`_rt_imageLayerComposite`、文本排版、`camerafade`、
   `animationlayers`、音频频谱 —— 六项因此**避免做无用功** ✓）
3. **先查参考是否实现** ✓（文本排版：参考自己写着 "Phase 2" ✓）
4. **能力矩阵的每一行都要用真包核对** ✓（文本格式曾"按代码意图"误判 ✓）
5. **不允许可见回归** ✓（对象摆放 4 次尝试均按此不落地 ✓）

## 对象摆放：用探针实测定案（2026-10-14）

**方法**：造"探针壁纸"——把一张图放在**已知 `origin`**，用**参考**渲染，再量它的落点/范围。
（要点：参考的纹理路径是 `materials/<名字>.tex`，材质里写**不带扩展名**的名字；场景文件必须能被覆盖，
即该壁纸的 `scene.pkg` 不能挂载成功——`3003908979` 可以，`2178468632` 不行 ✗。）

**实测结论（逐项吻合）**

| 项 | 实测 |
|---|---|
| 四边形尺寸 | **纹理真实尺寸 × scale** ✓ —— scene.json 的 `size` 字段**被忽略** ✗（声明 4272x2848 的 `clouds 1` 实际按纹理 1920x1080 画 ✓） |
| `origin` 语义 | 四边形**中心**，画布像素，**Y 自底向上** ✓（origin.y=270 → 中心在窗口 y=810 ✓） |
| 画布 | `general.orthogonalprojection` ✓，再按 `窗口/画布` 缩放 ✓（投影 3840x2160 + 窗口 1920x1080 → 坐标与尺寸都减半 ✓） |
| 模型 `fullscreen` | 尺寸=场景尺寸 + origin=场景中心 ✓（语料 0 个 ✗） |
| `passthrough` 层 | **四边形恒为全屏** ✓（`composelayer.vert` 用 texcoord 生成 `gl_Position` ✓），采样坐标来自**对象场景位置经 MVP** ✓ |

→ 该数学已落地为 `xw_scene_object_rect()` **+ 单元测试** ✓（数值即上表实测值 ✓），**但未接线** ✗（原因见下）。

**为什么仍不能接线** ✗

- 接上后 2178468632：**0.0271 → 0.0776** ✗ —— `night blue` 的 origin.x=948.114 使四边形左移 12px，
  右边缘露出 **12px 亮条**（x≥1908 均值 176 ✗ vs 参考 14 ✓）。
- **参考没有这条带、也没有这个平移** ✓：上半部灯柱列位置 —— 参考 **859**、我们无摆放 **853**、我们有摆放 **844** ✗。
- 参考的等效行为与"裸数学"不同 ✗，缺的是 **`passthrough` compose 层**：
  它们把场景（FFB）**按对象矩形映射到全屏**再叠回 ✓，并带**特效链** ✓。
  我按"全屏拷贝"近似接上 → **0.2812** ✗（特效链处理这些层会大幅走样 ✗）→ 已回退 ✓。

**顺带查出两个真 bug**（未落地，是下一步的前提）

1. **引擎资产材质/模型取不到** ✗：`materials/util/*`、`models/util/*` 只在资产树里 ✓，
   而 `slurp_json` 只在壁纸目录找 ✗ → compose 层材质解析失败 → `mat_tex` 为空 → **整层不画** ✗。
2. **材质的 `_rt_*` 纹理引用未处理** ✗：`materials/util/composelayer.json` 的 `"textures": ["_rt_FullFrameBuffer"]` ✓
   被当成普通纹理 ✗ → 层被丢弃 ✗。参考把它当**当前场景**采样 ✓。

**另**：`visible:false` 的 `night alpha`/`clouds 1`/`clouds 2` 是**作者数据** ✓，参考同样不画 ✓ —— 我们的解析**是对的** ✓。

## 摆放接线尝试与 compose 层：全部实测数据（2026-10-14 续）

**试过的组合与实测（`XW_FIXED_TIME`，与参考同帧比 RMSE）**

| 组合 | 2178468632 | 3003908979 | 2887099508 |
|---|---|---|---|
| 基线（不摆放、无 compose 层） | **0.0271** | **0.1961** | **0.3205** |
| 仅资产回落 | 0.0271 ✓ | **0.2204** ✗ | — |
| 资产回落 + compose 层 + 翻转修正 | 0.0271 ✓ | 0.1961 ✓ | **0.3195**（但对基线差 0.2830 ✗） |
| 再加摆放 | **0.0776** ✗ | 0.1961 ✓ | 0.3066 ✗ |

**逐条结论**

1. **`_rt_FullFrameBuffer` 的方向问题已定位并解决** ✓：窗口快照是"窗口方向" ✓，而对象通道每级翻转一次 ✓，
   基础拷贝必须**反向翻转**一次 ✓（`xw_gl_draw_window_source` ✓）→ 修正后 2178468632/3003908979 **逐位不变** ✓。
2. **compose 层（`passthrough`）在 2887099508 上会摧毁画面** ✗：人物消失、只剩两个色块 ✗ ——
   它们的特效（`effects/blend` ✓）带**脚本化常数** ✓，而我们的属性求值未接线 ✗ → 常数用错 → 整层覆盖 ✗。
   **必须先把常数/属性求值做对** ✗ 才能落地 compose 层。
3. **摆放的 12px 条带仍未解决** ✗：条带 = 我们的**清屏色 0.7 灰**（右边缘均值 176 ✓）；
   参考右边缘均值 ≈14–15 ✓（**无条带** ✓）。`night blue` 的 origin.x=948.114 → 四边形左移 12px ✓，
   而 compose 层是 identity 映射 ✓（全屏 ✓ 按对象矩形采样 ✓）→ **盖不住** ✗。
   参考为何没有条带仍未定案 ✗（下一个可查点：参考的**对象 FBO** 是否清屏/如何合成到窗口 ✓）。
4. **资产回落单独不落地** ✗：它让 `materials/util/*` 解析成功 ✓ → 一些原本被跳过的 util 层开始绘制 ✗
   → 3003908979 变差 ✗ → 必须与 util 材质语义（solidlayer/passthrough ✓）一起做 ✓。

**本次全部回退** ✓（`xw_scene_object_rect` + 单测已在上一提交落地 ✓）。**下一步顺序**：属性求值 → compose 层 → 摆放。

## 属性求值接线（2026-10-14 续 2）

**落地** ✓：对象脚本化属性（`origin`/`scale`/`size`/`visible`）现在**每帧求值并写回对象** ✓。

- 解析端保留每个属性的 `script` 源码 ✓（`XWSceneObject::prop_script[]` ✓，`script_of()` ✓，随对象释放 ✓）
- 脚本层持有**每对象每属性**的运行器缓存 ✓（`XWObjPropRunners` ✓，`set_scene` 换场景时丢弃 ✓）
- `xw_script_apply_object_props()` ✓ 在渲染前逐帧调用 ✓；`visible` 传**布尔** ✓（`xw_script_prop_update_bool` ✓），
  抛错或无 `update()` 的脚本**保留解析值** ✓
- 单测：`tests/test_scene_script.c::test_apply_object_props` ✓（含"抛错不改值" ✓ 与"以上一帧值为输入继续" ✓）

**实测（变更前 vs 变更后，`XW_FIXED_TIME` 25 帧）**

| 壁纸 | 差异 |
|---|---|
| 2178468632 / 2887099508 | **0** ✓（后者的 27 个脚本被混淆、需 `thisScene` ✓） |
| 2794098047 | 0.00021 ✓ |
| 3422426571 | 0.00198 ✓ |

**验证了脚本产出的正确性** ✓：`2794098047` 的"秒数"用 `new Date().getSeconds()/60` 驱动 scale ✓；
"音频"用 `scriptProperties` 滑块把 origin.y 从陈旧的 **238 修正为 768** ✓。

**收益仍受两道门限制** ✗：`origin/scale/size` 要等**摆放** ✓；`visible` 的脚本（`3808922316`/`2887099508` ✓）
需要 **`thisScene.getLayer/getLayerIndex/sortLayer`** ✓ —— 这是下一个增量 ✓。

## thisScene 脚本 API + init()（2026-10-14 续 3）

**语料实测的 API 面** ✓：`getLayer`(6) / `getLayerIndex`(5) / `sortLayer`(5) / `getInitialLayerConfig`(4) / `createLayer`(3)；
`function init(` 出现 **40** 次 ✓（`update(` 82 次 ✓）。

**参考实现** ✓：`ScriptEngine.cpp:481` 会调 **`init()`** ✓（`callHook("_init")` ✓）；
但 `thisScene` 只实现了 `getLayer` ✗（`SceneObject.cpp:307` "TODO: ADD REST OF THE METHODS" ✗）→
**没有** `getLayerIndex`/`sortLayer` ✗ → 我们实现它们会比参考更贴近真实 WE ✓（在参考基准上表现为"偏离" ✓，已如实测量 ✓）。

**落地** ✓
- **`init()` 被调用一次** ✓（与引擎/参考一致 ✓）：脚本抛错则忽略 ✓（同引擎 ✓）
- `thisScene.getLayer(name|index)` ✓ / `getLayerIndex(handle)` ✓ / `sortLayer(handle, at)` ✓ / `getLayerCount()` ✓
- `thisLayer` ✓（含 `__idx` 与 `name` ✓；未知属性读写无害 ✓，引擎的图层对象远超我们建模的范围 ✓）
- **渲染序改由 `XWScene` 持有** ✓（`order`/`n_order` ✓）—— 修复了一个真 bug：`build_order` 会重建数组 ✗，
  属性脚本持有的指针会失效 ✗
- 单测 ✓：`test_this_scene_layers` ✓（脚本 `init()` 把 "b" 移到索引 0 ✓）

**实测（与上一版对比）**

| 壁纸 | 差异 | 说明 |
|---|---|---|
| 2178468632 / 3808922316 / 3610097897 / 2887099508 | **0** ✓ | 3808922316 的排序**确实发生** ✓（调试可见 `layer 34 moved from 34 to 33` ✓）但这两层顺序不影响画面 ✓ |
| 3422426571 | 0.00197 ✓ | 对参考 **0.376881 → 0.376882** ✓ 中性 ✓ |
| 2794098047 | 0.00690 ✓ | **修正** ✓：其 `init(value){ initialValue = value.x; }` ✓ 是 `update` 平滑算法的基准 ✓，不调 `init` 时该值缺失 ✗（参考无法渲染此包 ✗，无法对照 ✓） |

**未实现**（保守 ✓，脚本抛错即忽略 ✓）：`createLayer` ✗、`getInitialLayerConfig` ✗（动态建层 ✗）、模块系统（`import WEVector` ✗）、`engine` ✗。

## engine API + 模块系统（2026-10-14 续 4）

**语料实测的 API 面** ✓：`engine.runtime`(18) / `engine.canvasSize`(4) / `engine.registerAudioBuffers`(2) /
`engine.AUDIO_RESOLUTION_16`(1) / `engine.frametime`(1)；模块只有 **`WEVector`**(1) 与 **`WEMath`**(1) ✓。

**落地** ✓
- `engine` ✓：`runtime`（秒 ✓，`xw_script_now_ms()/1000` ✓）、`frametime`（本帧 dt 秒 ✓）、
  `canvasSize`（= 场景正交投影尺寸 ✓）、`registerAudioBuffers(res)`（返回全 0 的 `{average,left,right}` ✓）、
  `AUDIO_RESOLUTION_16/32/64` ✓
- `WEVector` ✓（`angleVector2` 及 add/subtract/scale/length/normalize ✓）、`WEMath` ✓（mix/clamp/lerp/radians/degrees ✓）
- **模块系统** ✓：`import ... from '…';` 语句在预处理时**剥离** ✓（引擎内建模块在这里就是全局对象 ✓）
- 帧值接线 ✓：`xw_script_set_frame()` ✓ 在 `render_overlays` 里（dt 所在处 ✓）于属性脚本运行**之前**调用 ✓
- 单测 ✓：`test_engine_api` ✓（`import WEVector` + `canvasSize` + `runtime`/`frametime` + 音频缓冲 ✓，数值断言 ✓）

**实测（与上一版对比）**

| 壁纸 | 差异 | 判定 |
|---|---|---|
| 2178468632 / 3003908979 / 2887099508 / 3610097897 / 3808922316 | **0** ✓ | 未用到新 API ✓ |
| 3422426571 | 0.00133 ✓ | 其 `update` 用 `engine.canvasSize` ✓；对参考 **0.376882 → 0.376882** ✓ 中性 ✓ |
| 2794098047 | 0.00396 ✓ | 其平滑算法用 `engine.frametime` ✓ |

**修正性质** ✓：此前 `engine.*` 是 `undefined` ✗ → `update` 返回 **NaN** ✗ → 我们跳过并保留基准值 ✓；
现在能**正确计算** ✓（`value.x = slider * canvasSize.x` ✓、`smoothValue += audioDelta * min(1, frametime*smoothing)` ✓）。
