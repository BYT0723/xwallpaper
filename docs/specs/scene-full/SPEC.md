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
