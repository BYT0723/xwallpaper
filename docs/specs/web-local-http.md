# Spec: web-local-http (目录壁纸走内嵌 localhost HTTP)

## Objective

Unity WebGL 导出的目录壁纸（如 `3814047032`）在 `file://` 下必然失败：
`Build/*.loader.js` 内写明 `file://` 不受支持，`framework.js` 的 wasm
`fetch` 在 `file://` 下拿不到 `Content-Type`/`Content-Length`（实测
`fetch('app.wasm')` 在 `file://` 下 `ct=null;len=null;bytes=8`），回退到
`abort("both async and sync fetching of the wasm failed")`。韩文文件名
（`Build/탐정_고차비.wasm`）本身无辜，换 ASCII 名同样失败。

User stories:
- `xwallpaper set --web <Unity目录>` 后壁纸正常加载，不再 abort。
- 普通目录壁纸（`tests/data/webdir`）行为不变，像素回归测试仍过。
- daemon 重启/`restore` 后自动恢复，不依赖手工 `http.server`。

## Tech Stack

- C11 + 已有 `glib/gio`（随 GTK 栈来，**不新增构建依赖**，CLI 仍只链 libc）。
- `src/web_http.[ch]`（new, daemon-only）：每张壁纸一个 `XWWebHttp`，
  `GSocketService` 绑 `127.0.0.1:0`（临时端口，内核分配），root 为壁纸目录。
- 线程模型（教训：第一版在派发上下文里阻塞读，被 WebKit 预连接饿死、
  主循环 stall）：service 跑在**独立 server 线程**（私有 `GMainContext`），
  `incoming` 只做非阻塞分发，每连接一个 detach 工作线程；读超时 10s；
  握手/啟停用 `pthread_mutex/cond`（`GMutex` 的 futex 实现 TSan 不可见，
  故用 pthread 以便 TSan 验证）。
- 语义：`GET/HEAD`，MIME 表（含 `wasm→application/wasm`）、`Content-Length`
  必带、`Accept-Ranges: bytes`、单 `Range` 回 `206`、目录穿越（`..`、
  `%2e`、root 外 symlink）回 `403`、不存在回 `404`。64KB 分块发送，
  整文件不进内存。
- 安全：只听回环，对外不可见；端口随机；生命周期随壁纸
 （`init/start` 起、`detach/destroy` 停、`reload` 换 root 重建）。

## Project Structure

```
src/web_http.[ch] (new) → 服务 + 纯逻辑（mime/resolve/parse_range，可单测）
src/web.c               → 改：目录模式 `web_start` 改走
                          `http://127.0.0.1:<port>/<entry>`；`detach/destroy` 停服
tests/test_web_http.c (new) → 单测：mime/路径净化/range 解析
tests/data/webunity/ (new)  → fixture：`index.html` + 8 字节 `app.wasm`，
  页内 `fetch` 后把 `Content-Type/Length/bytes` 写进 title
tests/integration/test_web.c → 加用例：URI 以 `http://127.0.0.1/` 开头且
  title 为 `WASM:ct=application/wasm;len=8;bytes=8`（`file://` 下为
  `ct=null;len=null`，天然红灯守卫）
```

## Testing Strategy

- `make test`：`test_web_http` 3/3；其余单测全绿（`test_client` 在本机因
  live daemon 占 socket 目录失败，基线同错，属环境问题）。
- `make test-xvfb`：`test_web` 12/12、`test_web_dpms` 1/1、
  `test_web_audio` 1/1。
- `make check-client-libs`：OK。
- TSan：6 并发 + 4 idle 连接零告警；ASan+UBSan 单测干净。
- curl 实测真实 80MB 壁纸目录：韩文 `.wasm` 206 + `application/wasm` +
  `Content-Range: bytes 0-7/40057359`，`.data` HEAD 43MB，`..%2f` 回 403。

## Boundaries

- 修订 `web-dir-wallpaper.md` 的 Never 条款：允许 `127.0.0.1` 临时端口，
  仍禁止外部进程 server、固定端口监听、新构建依赖、动 CLI 链接。
- Always：主循环永不阻塞（任何 idle peer 只能拖住自己的工作线程 10s）；
  `destroy` 有界等待（30s cap，正常为 0）。
- WE file 属性：绝对路径/`file://` 选取在投递时翻译为
  `http://127.0.0.1:<port>/_xw/file/<id>` 别名（server 内注册，仅既存常规
  文件，最多 64 个，随壁纸释放）；存储/`list` 保持用户原始值；相对路径
  与 http(s) 值直通（相对文档 URL 解析，本来就可用）。
- 稳定源：每壁纸的端口存 sidecar（`props/<sha>.port`，0600，原子写），
  重启复用，被占则回退临时端口且不覆盖旧映射；SO_REUSEADDR 保证
  TIME_WAIT 下仍能 rebind。WebKit 存储（Cache/IndexedDB/localStorage）
  从此跨重启有效。
- 一次性 file→http 迁移：file 时代的 localStorage 以 `file://` 源键存于
  同数据目录，切 http 后不可见。marker（`props/<sha>.migrated`）守卫，
  每壁纸只跑一次：file 载入口→dump→http 载入→写回→reload 一次让页面
  正常读取。file 源优先（http 孤儿只会有页面盲写的默认值）；相对路径
  与空存储直接标记完成。load-failed 则回退 http 永不黑屏。
