# Spec: web-engine-eval（web 后端引擎选型评估）

## Objective

把「要不要换掉 webkit2gtk-4.1」从直觉判断变成可复核的数据 + 明确的决策门槛：

- 量化现状（4.1）在真机 GPU / 软渲染 / 真实 daemon 三种口径下的成本；
- 量化唯一可同口径对比的候选（WebKitGTK 6.0）；
- 对 NetSurf / litehtml / Ultralight / WPE / CEF 给出**能力**结论（能不能跑 WE 壁纸）与**工程**结论（要改多少代码）；
- 给出触发迁移的判据，避免"为了省内存换引擎"这类无收益动作。

非目标：本轮不迁移引擎、不改 `src/web.c` 行为、不新增构建依赖。

## 结论（TL;DR）

1. **小引擎出局不是"资源更低"，是"跑不了壁纸"**：NetSurf / litehtml / Ultralight 都无法承载
   Wallpaper Engine 页面的硬需求（WebGL2、WebAssembly、`<video>`/MSE、Web Audio、可编程注入与求值）。
   Ultralight 尤其容易误判——它自称 WebKit fork、Safari 16.4 基线，但**不支持 WebGL、
   不支持 Web Audio**，免费版还有 60FPS 上限、无 JIT、无媒体播放，源码仅 Enterprise 可见。
2. **真正的候选只有三个**：WebKitGTK 6.0（同内核、换 GTK4 窗口层）、WPE WebKit（同内核、自己合成）、
   CEF/Chromium（兼容性天花板、体积与架构代价最大）。
3. **现在不换**。6.0 的收益是动画/画布/WebGL 类壁纸 CPU 降 48–64%，代价是常驻 PSS +70MB 左右、进程 +5，
   并且在**无 GPU 环境（VM / VNC / Xvfb 集成测试）反而更差**：动画类 CPU 137%→260%，
   canvas2d 60fps→40fps，webgl 38fps→30fps。
4. 迁移触发条件写死在第「决策门槛」节；在那之前，兼容性缺口继续用 shim + 本地 HTTP 补
   （`docs/specs/we-api-stubs.md`、`docs/specs/web-local-http.md` 已验证是低成本高收益路径）。

## 硬需求（从代码数出来的）

| 能力 | 代码依据 | 可否降级 |
|---|---|---|
| JS 执行 + 双向消息 | `src/web.c` 的 `evaluate_javascript`、`register_script_message_handler`、WE shim | 否，WE API 全靠它 |
| WebAssembly + JIT | `docs/specs/web-local-http.md`（Unity WebGL 导出） | 否，解释器跑不动 wasm 壁纸 |
| WebGL/WebGL2 + canvas 2D | three.js 类壁纸、粒子/着色器壁纸 | 否 |
| HTML5 视频 / MSE | `tests/data/webmedia`、`<video>` 壁纸 | 否 |
| Web Audio（AudioContext） | WE 音频可视化壁纸 | 否 |
| 文档级注入（user script / style） | `docs/specs/web-noninteractive.md` | 否，去交互化靠它 |
| 每壁纸独立上下文/数据目录 | `web.c` 的 `webkit_web_context_new_with_website_data_manager`、file-access 开关 | 可退化为全局共享（有代价） |
| 把渲染结果放进任意 X11 窗口 | `XReparentWindow` + `gtk_window_*`（X11）、`gtk_layer_*`（Wayland） | 否 |

一句话：**这是"完整浏览器内核 + 可嵌入"的需求，不是"HTML 渲染器"的需求。**

## 方案能力核查

| 方案 | 原始对比表里的说法 | 实际（对 WE 壁纸） | 证据 |
|---|---|---|---|
| NetSurf | JS 有限 | 不是可嵌入引擎，是自带 frontend 的浏览器；duktape 实现的 ES5 级 JS 子集，无 wasm / WebGL / MSE，渲染走自有 framebuffer | 源码结构；无嵌入 API |
| litehtml | JS 无 | 只是 HTML/CSS 布局+渲染库：DOM、JS、网络、图片解码、媒体全要自己给 | 上游 README |
| Ultralight | JS 有 / 现代 Web 较好 / 资源低 | v2.0 是 WebKit fork（Safari 16.4 基线），但**无 WebGL、无 Web Audio**；免费版 60FPS 上限 + 无 JIT + 无媒体播放；源码仅 Enterprise；按标题授权 $395/$4500 | 上游 *Supported Web Features* 与 *Pricing* 页 |
| WPE WebKit | 资源中低 | 内核与 WebKitGTK **同一份**（2.54.2），内存/进程模型同级；省的是 GTK 依赖与一次拷贝 | `pacman -Si wpewebkit` = 2.54.2 |
| WebKitGTK | 资源较高 | 对，但"较高"来自多进程 + 映射库，不是引擎本身笨重；见下方实测 | 本机 2.54.2 |

Ultralight 的"Linux 15MB"是真的，但它省掉的正是本项目的全部需求；同时它是**闭源商业 SDK**，
开源项目按标题买授权不成立。**结论：不进入候选。**

## 实测方法

工具在 `bench/web-engine/`（不接入 `make`，不新增构建依赖）：

- `wbench.c` — 同一份源码编译两次：`webkit2gtk-4.1 + GTK3` 与 `webkitgtk-6.0 + GTK4`。
  载入合成页面，报 `load_ms / first_frame_ms / fps / cpu_pct / procs / pss_mb`，单行 JSON。
  页面模式：`blank`、`anim`（CSS 动画+文本）、`canvas2d`、`webgl`、`js`（300 万次热循环 + wasm 校验）、`video`。
- `run.sh` — 矩阵（引擎 × 模式 × N 次）→ `summarize.py` 取中位数。
- `daemon-cost.sh` — 真实 `xwallpaperd`：每个后端起一个**全新 daemon**（独立 XDG 目录），避免上一个后端的堆残留污染下一个数字。
- 口径：**PSS 而非 RSS**。RSS 会把 GTK/WebKit 共享库页在每个进程里重复计一遍（实测同一个页面 RSS 453MB vs PSS 250MB）。

环境：WebKitGTK 2.54.2 / GTK 3.24.52 / GTK4 4.24.1，16 核，eDP 3640×1920（:0 有 GPU），
窗口 1920×1080，采样 5s（预热 1s），每点 3 次取中位数。:0 与 Xvfb 分开测。

## 实测结果

### 1) 引擎级 · 真机 GPU（:0）

| 模式 | 引擎 | load_ms | first_frame_ms | fps | cpu_pct | 进程 | PSS MB |
|---|---|---|---|---|---|---|---|
| blank | 4.1 | 240 | 241 | 61.8 | 5.6 | 3 | 158 |
| blank | 6.0 | 256 | 256 | 61.6 | 5.6 | 8 | 237 |
| anim | 4.1 | 253 | 274 | 61.8 | 34.4 | 3 | 175 |
| anim | 6.0 | 278 | 279 | 61.8 | 17.7 | 8 | 242 |
| canvas2d | 4.1 | 238 | 240 | 59.6 | 39.6 | 3 | 169 |
| canvas2d | 6.0 | 288 | 289 | 59.6 | 20.4 | 8 | 242 |
| webgl | 4.1 | 293 | 293 | 58.9 | 56.9 | 3 | 173 |
| webgl | 6.0 | 298 | 299 | 59.6 | 20.7 | 8 | 247 |
| js（JIT） | 4.1 | 296 | 296 | 61.7 | 5.6 | 3 | 166 |
| js（JIT） | 6.0 | 275 | 276 | 61.8 | 5.6 | 8 | 237 |

`js` 模式 300 万次 Float64Array 循环两者都是 5–9ms → 两个 ABI 都有 JIT（解释器会是 100ms 量级）。

### 2) 引擎级 · Xvfb + llvmpipe（无 GPU）

| 模式 | 引擎 | fps | cpu_pct | PSS MB |
|---|---|---|---|---|
| blank | 4.1 / 6.0 | 61.9 / 61.4 | 4.6 / 5.7 | 327 / 346 |
| anim | 4.1 / 6.0 | 62.0 / 61.0 | 137 / 260 | 354 / 375 |
| canvas2d | 4.1 / 6.0 | 59.7 / **39.5** | 447 / 557 | 361 / 388 |
| webgl | 4.1 / 6.0 | **38.0 / 29.7** | 737 / 861 | 390 / 467 |

软渲染下 6.0 走 Skia+GPU 进程再落到 llvmpipe，比 4.1 的路径贵一倍 CPU 并掉帧。
同机多次重复方差较大（负载 5+），表中为 3 次中位数，只用于判断方向。

### 3) daemon 级 · 真实 `xwallpaperd`（:0，每个后端全新 daemon）

| 场景 | 进程 | PSS MB | 说明 |
|---|---|---|---|
| idle（无壁纸） | 1 | 38 | 基座 |
| image | 1 | 52 | +14MB |
| video（mpv） | 1 | 151 | +113MB |
| web（`tests/data/webdir`） | 3 | 250 | +212MB，含 WebKitWebProcess + NetworkProcess |
| web（`tests/data/webunity`，走本地 HTTP + wasm） | 3 | 245 | wasm 不多花钱，钱花在进程上 |
| web → `clear` 后 15s | 1 | 114 | web 进程已退出，但 ~76MB 映射页/缓存不归还 |

Xvfb 下同一组数字：web 374MB、clear 后 132MB（llvmpipe 的 GL 上下文与缓冲区额外 ~120MB）。

### 解读

1. **web 后端的固定成本是「一个 WebKitWebProcess + 一个 NetworkProcess」**（本机 ≈ 210MB PSS / 2 进程），
   与本机内核版本无关；换 WPE 或 6.0 都不会让它变小。
2. **6.0 在真机上用内存换 CPU**：动画/画布/WebGL 类壁纸 CPU −48%…−64%，代价 PSS +67…+79MB、进程 +5。
   对"多屏常驻壁纸"这个场景，省的是持续功耗，费的是常驻内存。
3. **6.0 在无 GPU 环境明显更差**：动画类 CPU 137%→260%，canvas2d 59.7→39.5fps，
   webgl 38.0→29.7fps。本项目的集成测试 `make test-xvfb` 正跑在 Xvfb 上，VM/VNC 用户也属于这一类，
   所以 6.0 若成为默认，必须保留 4.1 的编译选项。
4. `clear` 后 76MB 不归还、web 进程会退出：这是可优化项（壁纸全部清空后销毁 context/network session），
   与引擎选型无关。
5. 引擎级 PSS（175MB）低于 daemon 级（250MB）的差额 = daemon 自身基座 + 每壁纸独立上下文的开销，
   说明"每壁纸独立 context"有真实成本，但换来 file-access 隔离与独立缓存，暂时值得。

## 迁移工作量清单（4.1 → 6.0）

用 `web.c` 里出现的符号对 GTK4/6.0 头文件做差集，得到的**完整**缺口：

| 4.1 用法 | 6.0 替代 | 位置 |
|---|---|---|
| `webkit_web_view_new_with_context` | `g_object_new(WEBKIT_TYPE_WEB_VIEW, "web-context", ctx, NULL)`（该属性 construct-only） | `web.c` 2 处 |
| `webkit_web_context_new_with_website_data_manager` + `webkit_website_data_manager_new` | `webkit_network_session_new(data_dir, cache_dir)` + construct-only `network-session` | `web.c` 上下文构建 |
| `register_script_message_handler(ucm, name)` | `(ucm, name, world_name)`，默认世界传 `NULL` | `web.c` 3 处 |
| `WebKitJavascriptResult` | 信号直接给 `JSCValue*` | 消息回调 |
| `gtk_container_add` / `gtk_widget_show_all` / `gtk_widget_destroy` / `gtk_widget_get_window` | `gtk_window_set_child` / `gtk_widget_set_visible` / `gtk_window_destroy` / `gtk_native_get_surface` | 窗口层 |
| `gtk_window_set_skip_taskbar_hint` / `skip_pager_hint` | GTK4 无对应，改用 X11 属性直接设 | 窗口层 |
| `gdk_window_reparent` / `gdk_window_set_events` / `gdk_window_get_events` | `gdk_x11_surface_get_xid` + `XReparentWindow` / `XSelectInput` | 窗口层 |
| `gdk_display_get_monitor` / `gdk_display_get_n_monitors` | `gdk_display_get_monitors()` + `GListModel` | 显示器枚举 |
| `gtk_layer_init_for_window` 等 | `gtk4-layer-shell`（同名函数，另一套库） | Wayland 路径 |

好消息：`allow_file_access_from_file_urls` / `allow_universal_access_from_file_urls` 在 6.0 仍在，
`website policies`、`evaluate_javascript`、user script/style 也都在 → **file:// 目录壁纸与 WE 注入不需要改语义**。

## Spike 计划（4.1 → 6.0，可分段回退）

| 切片 | 做什么 | 完成判据 | 风险 |
|---|---|---|---|
| S0 构建开关 | Makefile 加 `WEBKIT_PC ?= webkit2gtk-4.1`，`pkg-config` 可切 `webkitgtk-6.0`；`dmain.c` 的 `gtk_init` 走宏 | 两个组合都能 `make`，`make check-client-libs` 仍过 | 无 |
| S1 窗口层 X11 | `XReparentWindow` 路径改成 GTK4 API + `gdk_x11_surface_get_xid` | `tests/integration/test_web` 在 6.0 下像素断言通过；`xwininfo -tree` 里壁纸窗口仍是应用窗口的子窗口 | GTK4 不承诺支持外来父窗口，可能只能靠 `XReparentWindow` 的现状行为 |
| S2 WebKit API | 上下文/消息回调按上表改；per-wallpaper context 用 construct-only 属性重建 | WE 注入用例、props、media、audio 全部集成用例在 6.0 下通过 | `network-session` 与旧 `WebsiteDataManager` 语义差异（缓存、cookie 目录） |
| S3 Wayland 路径 | `gtk4-layer-shell` 替换 `gtk-layer-shell` | Wayland 手测（layer 上屏、键盘模式） | 需要另一套库与真机验证 |
| S4 回归与取舍 | 跑本文件的测量口径，确认 CPU/PSS 变化与 Xvfb 退化幅度 | 数据写回本文件；决定是否默认切 6.0 | Xvfb 下性能退化可能让 `make test-xvfb` 超时，需要调采样/超时 |

## 决策门槛

| 触发条件 | 动作 |
|---|---|
| 发行版移除 GTK3 版 webkit2gtk-4.1（或在用发行版已标记废弃） | 启动 S0–S2 |
| 多屏 + 复杂 WebGL 壁纸的持续 CPU 成为抱怨点，且用户机器都有 GPU | 启动 S0–S2，默认切 6.0，保留 4.1 作为无 GPU 环境的编译选项 |
| 需要 DMABUF 零拷贝 / 彻底去 GTK / 自定义帧节奏 | 才评估 WPE（等于重写 `web.c` 窗口层 + 自建 GL 合成 + 输入转发） |
| 某个 WE 壁纸在 WebKit 上确实修不好且用户量大（Chromium 专属行为） | CEF 作为编译期可选逃生舱，先写资源预算再动手 |
| 只是"内存看起来高" | 不做引擎迁移；先做「web 壁纸清空后释放 context」这类局部优化 |

## Boundaries

- Always：新增测量工具只放 `bench/web-engine/`，不进默认 `make`，不给 CLI 引入任何依赖。
- Ask first：把 `bench/` 接入 `make test` 或 CI（会引入 GPU/Xvfb 依赖与耗时）。
- Never：为了"轻量"引入 NetSurf/litehtml/Ultralight 之类的降级引擎；为评估改 `src/web.c` 行为。

## 顺带发现（与本评估无关，另行跟踪）

本分支只新增文档与 `bench/`，未动 `src/`。但在这台机器上：

- `make test-xvfb` 在 `test_video_fps_override` 处崩溃退出（前两个 video 子用例通过），日志里有
  `_amdgpu_device_initialize: amdgpu_query_info(ACCEL_WORKING) failed (-13)` 与
  `MESA: vulkan: No DRI3 support detected`。
- 同样的形态在 `DISPLAY_SPEC=xvfb` 下 `xwallpaper set --video` 也能复现（daemon 收到 SIGSEGV）。

即 libmpv 在无 DRI3 的 Xvfb 上起不来，而 video 后端没有优雅降级。`daemon-cost.sh` 因此在 Xvfb 下
跳过 video 场景（真机 `:0` 下 video 正常，151MB PSS）。这条单独开任务修，不属于引擎选型。

## 复现

```sh
# 引擎级：真机 GPU / 软渲染
cd bench/web-engine
DISPLAY_SPEC=:0 REPS=3 ./run.sh                 # 会短暂弹窗，勿在有重要窗口时跑
REPS=3 ./run.sh                                  # Xvfb + llvmpipe

# daemon 级（每个后端一个全新 daemon）
DISPLAY_SPEC=:0 ./daemon-cost.sh
./daemon-cost.sh

# 只跑指定模式
MODES="webgl canvas2d" REPS=1 ./run.sh
```

原始数据：`bench/web-engine/results*/runs.jsonl`（每行一次运行）、`results*/daemon-cost.txt`。
