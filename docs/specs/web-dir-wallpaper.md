# Spec: web-dir-wallpaper (目录即壁纸，file:// + settings)

## Objective

让 `xwallpaper set --web <dir>` 把整个本地目录当作一张动态壁纸：
`xwallpaperd` 以 `file://<dir>/index.html` 加载，配合 WebKit 的两个
file-access 开关达到与 `python3 -m http.server` 等价的子资源语义，
且媒体走原生文件路径（可 seek），零外部进程、零 TCP 端口、生命周期随壁纸窗口。

User stories:
- 我有一个壁纸工程目录（含 `index.html` + `app.js`/`style.css`/图片/字体），`xwallpaper set -m HDMI-1 --web ~/wall/clock` 后该屏显示它，相对路径资源、`fetch(./data.json)`、`ES module` 正常工作。
- 我仍可用 `xwallpaper set --web https://example.com/page` 铺外网 URL，与本地目录共存。
- daemon 重启/`restore` 后目录壁纸自动恢复，不依赖任何手工起的 server。
- 不再支持单个 `.html` 文件直设；单页场景请包成目录（`index.html` + 资源），即目录模式已覆盖单页需求。

非目标：Wayland（`web_init_wl` 保持现状，后续对齐）；外网 URL 的离线缓存；目录内服务端逻辑（WS/CGI）；单个本地文件直设（本地文件一律报错，提示包成目录）。

## Tech Stack

- C11，现有 `webkit2gtk-4.1 + GTK3`，不新增构建依赖（不要 libsoup，不要 python 运行时依赖）。
- 关键 API：`WebKitSettings` 的 `allow-file-access-from-file-urls` +
  `allow-universal-access-from-file-urls`（每个壁纸独立 WebKitWebContext 上设置，
  见 `src/web.c:web_attach_view`）。
- 曾评估自定义 scheme（`xw://`）方案并已废弃：实测 WebKit 媒体栈不支持自定义
  scheme（`<video>` 必 `MEDIA_ERR_SRC_NOT_SUPPORTED`，range/206 也救不了），
  而 `file://` 媒体原生可用。教训：媒体必须走 `file://` 或 `http(s)://`。

## Commands

```
Build:      make
Test:       make test
Integ(X11): make test-xvfb
Dep guard:  make check-client-libs
Manual smoke:
  ./xwallpaperd &
  ./xwallpaper set -g 800x600+0+0 --name w1 --web tests/data/webdir
  ./xwallpaper --state        # w1 行为 web，path 为目录绝对路径
  ./xwallpaper set -g 800x600+0+0 --name w1 --web https://example.com/
  ./xwallpaper clear -m w1
  ./xwallpaper exit
```

## Project Structure

```
src/web.c              → 改：目录拼出 `file://<root>/index.html` 加载；`web_attach_view` 开两个 file-access 开关；外网 URL 直通
src/web_dir.[ch] (new)→ 新增：纯逻辑（is_http_url、归一化目录入口），供单测
src/cli.c              → 改：目录路径识别为 web（无扩展名但为已存在目录 → XW_BACKEND_WEB）
src/daemon.c           → 不动（path 透传已是 NUL 分隔，目录空格安全）
src/state.c            → 不动（存用户原始路径的绝对形式即可，恢复时重走同一解析）
tests/test_web_dir.c   → 新增单测（纯逻辑，无 X）
tests/integration/test_web.c → 扩展：加 dir wallpaper 上屏用例（复用现有像素采样模式）
tests/data/webdir/     → 新增 fixture：index.html + app.js（module） + data.json（fetch） + style.css
docs/specs/web-dir-wallpaper.md → 本文件
```

CLI 契约（最终形态）：
```
xwallpaper set [target] --web <dir | http(s)://...>
```
- `--web` + 本地目录 → 唯一本地模式（入口 `<dir>/index.html`，root = 该目录）。
- `--web` + 本地文件（含 `.html`）→ 报错（提示包成含 `index.html` 的目录）。
- `--web` + `http(s)://` → 直通 `load_uri`。
- 无 `--web` 时：目录路径自动判为 web（原来无扩展名直接 -1 报错，见 `src/cli.c:74-93`）；本地文件仍按扩展名判定，但 `.html` 不再进入 web（见 Task 2）。

## Code Style

```c
/* web_dir.h — 纯逻辑，无 GTK/WebKit/X11 依赖，可单测 */
int xw_web_is_http_url(const char *path);
int xw_web_resolve(const char *in, char *root, size_t rn, char *entry, size_t en);
/* root: serve 根目录绝对路径；entry: 恒为 index.html；返回 0=本地目录，1=外网URL，-1=非法（含本地文件） */
```

- 命名沿用 `xw_<module>_<action>`；错误沿用 `XWError`（`{code,msg}`，返回 int）。
- 安全说明：`allow-universal-access-from-file-urls` 让壁纸 JS 可 `fetch` 任意本地文件；
  作用域限于该壁纸的独立 context（随窗口销毁，不共享 cookie/缓存），视为运行本地页面的正常代价。
- 存储说明：每个壁纸的 context 由该壁纸独占的 WebsiteDataManager 落盘
  （`~/.local/share|cache/xwallpaper/web/<sha256>`，按壁纸 root/URL 哈希隔离），
  `localStorage`/IndexedDB 跨 daemon 重启保留——壁纸设置不再复原。

## Testing Strategy

- 单测（无 X，`make test`）：`tests/test_web_dir.c` 覆盖——http 判定、目录解析、本地文件拒绝、缺 `index.html` 报错。纯逻辑目标 ~90%。
- 集成（Xvfb，`make test-xvfb`）：扩展 `tests/integration/test_web.c`，用 `tests/data/webdir/`（index 引用 `app.js` module + `fetch data.json` 后改背景色）上屏，沿用现有“泵 GTK 3s + 中心像素采样”断言，证明子资源链路真的通了（这是 `file://` 默认策略下会失败、开两个 file-access 开关后通过的用例）。
- 媒体回归：VP9/`file://` 在 Xvfb 下 `readyState=4` 无错（见本 spec Tech Stack 教训）。
- 手动：上面 smoke 命令 + daemon 重启后 `restore` 自动恢复目录壁纸。
- 回归门：`make check-client-libs` 必须过（web 代码只进 `xwallpaperd`，CLI 保持只链 libc）。

## Boundaries

- Always: `make test` 全绿再合；`web.c` 不开第二个 X 连接（复用 `x11` 的）；`destroy` 释放 context/view/root 路径；无目录列表；`clear` 后对应 WebView 销毁即停服。
- Ask first: 新增构建依赖（本方案不新增，若要换 libsoup 需另议）；改 protocol 文本格式（本方案不需要）；动 `web_init_wl`。
- Never: `fork+exec python/http.server` 外部进程；绑 TCP 端口监听；把 webkit/mpv/imlib2 链进 `xwallpaper` CLI；提交 secret。

## Success Criteria

1. `set --web <dir>`（目录含 `index.html` + js/css/json）在 X11 上屏，子资源加载成功（集成测试像素断言过）。
2. `set --web <本地文件>`（含单个 `.html`）返回可读错误（提示包成目录），不黑屏；目录缺 `index.html` 同样可读错误。
3. `set --web https://…` 仍直通可用；`--state/--list` 中 web 条目的 path 为用户原始绝对路径。
4. daemon 重启后目录壁纸经 `restore` 自动恢复，无需手工 server。
5. `make test`、`make test-xvfb`（有 xvfb 时）、`make check-client-libs` 全过。
6. 目录内 `<video>`（如 VP9/webm）可加载解码（`readyState=4` 无错）；无外部 server、无监听端口。

## Open Questions

- 无（已定：入口只认 `index.html`；单文件模式已剔除）。
