# Spec: Wayland 支持 (wlr-layer-shell) — xwallpaperd 自适应

## ASSUMPTIONS（先确认，错了请纠正）

1. 仅支持 wlr-layer-shell 合成器（sway / Hyprland / labwc / wayfire）；GNOME 明确不支持，KDE 顺带兼容但不保证。
2. 同一个 `xwallpaperd` 二进制自适应：`WAYLAND_DISPLAY` 存在且 registry 有 `zwlr_layer_shell_v1` → 走 Wayland 路径，否则走现有 X11 路径。CLI（`xwallpaper`）与 socket 协议不变。
3. 三后端全量对等（image / video / web），`-m/--all/-g/--list/clear/exit/restore` 语义不变。
4. 系统 `wayland-protocols` 不带 `wlr-layer-shell-unstable-v1.xml`（已验证），需 vendor 一份 xml 到仓库并用 `wayland-scanner` 生成代码。
5. 本机无 Wayland 合成器（CI 只有 Xvfb），Wayland 路径以编译 + 单测 + 真机手测为准，不阻塞 `make test`。

## Objective

让 `xwallpaperd` 在 Wayland 会话下同样可用：每个输出一个 layer-shell background 表面，image / video / web 三后端渲染其中，CLI 用法与 X11 完全一致。

**User stories:**
- 作为 sway/Hyprland 用户，我 `xwallpaperd &` 后 `xwallpaper set photo.png` 得到图片壁纸（fill/contain/stretch/tile 语义与 X11 一致）。
- 我 `xwallpaper set clip.mp4 --mute` 得到循环静音视频壁纸。
- 我 `xwallpaper set index.html` 得到 live web 壁纸。
- 我可用 `-m DP-1` / `--all` / `-g WxH+X+Y --name n` 定向输出，`--list/clear/exit/restore` 行为与 X11 一致。
- 在 X11 会话下一切保持原样（零回归）。

## Tech Stack

- C11，`wayland-client` + vendored `wlr-layer-shell-unstable-v1` + `xdg-output-unstable-v1`（系统已有，用 `wayland-scanner` 生成）。
- image-wl：imlib2 解码 → `wl_shm` ARGB 缓冲（与 X11 的 XRender 路径并列，不动现有 `image.c` X11 分支）。
- video-wl：libmpv **render API**（`mpv_render_context` + OpenGL/EGL）渲染到 layer-shell 面的 EGL surface；mpv 不再用 `--wid`（Wayland 下不可用）。`vo=gpu-next` 语义、loop/mute/volume/rotate/fps/keybinds 选项保持。
- web-wl：GTK 原生 Wayland + `gtk-layer-shell`（新增依赖 Arch `gtk-layer-shell` / Debian `libgtk-layer-shell-dev`）：`GtkWindow + WebKitWebView`，`gtk_layer_init_for_window` + BACKGROUND 层 + 全锚点 + 独占区 -1。替代 X11 的 `XReparentWindow` 方案。
- 输出枚举：`wl_output` + `zxdg_output_manager_v1` 取逻辑名/尺寸（替代 xrandr）；`-g` 矩形按逻辑坐标创建无输出绑定表面（需合成器允许，可失败并报错）。

## Commands

```
Build:      make                 # xwallpaper + xwallpaperd（含 wayland 路径，需 wayland-client + gtk-layer-shell）
Run tests:  make test            # 单元：cli/geom/client/state 不变 + 新增 wl 输出映射单测（无 Wayland 也可跑）
Check deps: make check-client-libs   # CLI 仍只链 libc，不变
Clean:      make clean
Debug build: make DEBUG=1
Manual smoke（sway/Hyprland 真机）:
  ./xwallpaperd &
  ./xwallpaper set tests/data/red.png
  ./xwallpaper set tests/data/bounce.mp4 --mute
  ./xwallpaper set tests/data/page.html
  ./xwallpaper --list
  ./xwallpaper clear
Wayland protocol regen:
  make proto-wayland   # wayland-scanner private-code/client-header 从 vendor xml 生成（需显式调用，不污染默认构建）
```

## Project Structure

```
src/
  wl.h/.c            → Wayland 平台模块（对标 x11.c）：连接/registry/output 枚举/geometry 解析/layer-shell 表面创建/销毁/list 输出/event 占位
  proto/wlr-layer-shell-unstable-v1.xml  → vendored（上游 wlroots），生成代码入 build（不手写）
  image.c            → 增 Wayland 分支（shm attach），X11 分支不动
  video.c            → 增 Wayland 分支（mpv render API + EGL），X11 --wid 分支不动
  web.c              → 增 Wayland 分支（gtk-layer-shell），X11 reparent 分支不动
  app.c/.h           → 平台分发：启动时检测 WAYLAND_DISPLAY + layer-shell 可用性，选择 x11/wl 后端；窗口数组改为平台 tagged union（或并行数组），set/clear/list/restore 逻辑复用
  daemon.c           → 主 poll 循环同时监听 wayland fd（+ 既有 X fd / GTK ctx）；DPMS 暂停在 Wayland 下首版为 no-op（见 Open Questions）
  dmain.c            → 不变（仍调 xw_daemon_main）
tests/
  test_wl.c          → 输出映射/几何解析单测：用伪造的 wl_output 列表测 -m/--all/-g/primary 解析（不连合成器）
  integration/test_wl_* → 有合成器时才跑（headless weston/sway），缺席则跳过（对标 test-xvfb 模式）
docs/specs/wayland.md → 本文件
```

## Code Style

```c
/* wl.h — 对标 x11.h 的平台 API，app 只认这组签名 */
int xw_wl_resolve_geometry(const XWWlOutput *outs, int n,
                           const XWConfig *cfg, int *x, int *y,
                           unsigned *w, unsigned *h, char *out_name);
XWWlWindow *xw_wl_create_window(xw_wl_ctx *ctx, const XWConfig *cfg,
                               int x, int y, unsigned w, unsigned h,
                               XWError *err);
void xw_wl_destroy_window(XWWlWindow *win);
```

- 命名：`xw_wl_*` / 类型 `XWWl*`；C11；无注释除非解释 Wayland 非 очевидно trick（如 layer-shell anchor/exclusive-zone 组合）。
- 错误：沿用 `XWError`，`int` 返回（0 = ok）。
- 平台判定集中在一处（`app`/`daemon` 启动），backends 不得自己 getenv 做分发（由 app 传入平台）。
- X11 现有文件零行为变更：Wayland 分支用 `if (app->use_wayland)` 包裹，X11 路径编译产物与现在一致。

## Testing Strategy

- **Unit（无合成器可跑，`make test` 必过）：** `test_wl` — 输出名查找、primary 回退、unknown 输出报错信息、`-g` 直通、`--all` 展开为 N 个目标。伪造 output 列表，不连 Wayland。
- **集成（有合成器才跑）：** `make test-wayland` — headless 合成器（weston headless 或 sway headless，若不可用则跳过并打印 SKIP）：layer-shell 表面创建、image shm attach 不崩、video render 初始化不崩、web GtkWindow 初始化不崩。对标 `make test-xvfb` 的 skip 语义。
- **手动（真机 sway/Hyprland）：** 上述 smoke 命令 + 多显示器 `-m/--all` + 替换（同一输出 set 两次无残留表面）+ `clear` 后 daemon 存活 + `exit` 退出。
- 覆盖期望：`wl.c` 纯逻辑（resolve/list 格式化）~90%；合成器交互为 crash-level；视觉行为手动。

## Boundaries

- Always: `make test` 通过后才合；CLI 薄（只链 libc，`make check-client-libs`）；Wayland 路径失败必须返回可读错误（含合成器是否缺 layer-shell 的提示），不得静默回退到 X11；释放每个 surface/buffer/EGL 上下文。
- Ask first: 新增构建依赖（gtk-layer-shell、wayland-scanner 生成步骤）；改动 socket 协议；改动 X11 路径行为；Wayland 下 DPMS/锁屏暂停方案。
- Never: 动 X11 桌面层语义；CLI 链接 wayland/mpv/webkit；vendor 的 wlr xml 私自改 protocol（只允许原样同步上游）；fork+exec 外部播放器。

## Success Criteria

- X11 会话：`make test` + `make test-xvfb` + `make check-client-libs` 全绿，与改前一致（零回归）。
- Wayland（sway/Hyprland 真机）：`set <png/mp4/html>` 三后端均显示；`-m/--all/-g` 定位正确，unknown `-m` 报错并列出输出；`set` 两次同输出原位替换无残留；`clear` 清表面但 daemon 存活；`exit` 退出；`--list` 显示输出名。
- 无合成器环境：`make test` 仍全绿；daemon 在只有 X11 时走 X11 路径，在两者都没有时报可读错误退出。
- 无 `wlr-layer-shell` 的合成器（如 GNOME）：daemon 启动（或 set 时）报明确错误 `compositor does not support wlr-layer-shell`，不崩溃。

## Open Questions

- `-g` 在严格平铺合成器上的允许程度（可能被忽略/拒绝 → 报错文案待真机确定）。
- 视频 DPMS/锁屏暂停在 Wayland 下的对等机制（首版 no-op：`ext-idle-notify` 后续再议，不阻塞 MVP）。
- `gtk-layer-shell` 打包名跨发行版差异（Arch `gtk-layer-shell` vs Debian `libgtk-layer-shell-dev`）→ README 安装表待补。
- HiDPI（fractional-scale）首版按逻辑像素处理，物理像素对齐后续优化。

## Span（Wayland image 横跨多输出，后加需求）

**动机**：`-g` 是 X11 全局 Screen 坐标系的产物；Wayland 无全局坐标，`-g` 语义退化为“无绑定单 surface（合成器定位置）”。真横跨（全景图连续铺多屏）需 app 层按输出拆分。

**语义**：
```
xwallpaper set --span WxH+X+Y <image>   # 布局空间矩形（xdg-output 逻辑坐标系）
```
- 矩形与每个输出求交，非空交集 → 每输出一个 surface（身份即输出名，与 `--all` 一致，`clear/-m/--list/state` 全部复用）。
- 每片渲染该片在整图中的裁剪部分（scale 模式作用于整个 span 矩形，再取片）。
- 仅 image：`--span` 配 video/web 直接可读错误（CLI 即时 + daemon 兜底）。
- `-g` 保留（脚本兼容），文档注明 Wayland 语义差异；互斥：`--span` 与 `-g/-m/--all` 不可同用。
- 部分输出覆盖：片 surface 用 TOP|LEFT 锚点 + margin 定位 + exclusive 0（非全屏片）。
- 已知限制（MVP）：daemon 重启后 span 片按普通 output 窗口恢复（整图，非裁剪），文档注明。

**协议/状态**：`set` 消息加 `target=span + span=WxH+X+Y`（line 协议向后兼容：未知 target 老 daemon 拒绝）；state 不扩展（片即 output 条目）。

**成功标准**：双屏布局下 `--span` 全景图视觉连续；单屏交集外的输出无 surface；video/web 配 span 秒级可读报错；X11 行为零变化（`--span` 在 X11 下同样可用，走 Screen 坐标系拆分？首版仅 Wayland，X11 传 span 报 not-supported——待定，见任务）。

**决议（2026-09-09）**：X11 传 span 报 `--span is only supported on Wayland`（已实现并验证 rc=1）；CLI 对 video/web 配 span 即时拦截；daemon 侧兜底同文案；daemon error 回复不再被 CLI 静默吞掉（main.c 修）。真机（niri）待用户验证：span 连续性、margin 定位支持度（不支持则退化整输出交集）。
