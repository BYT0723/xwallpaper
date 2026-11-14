# Spec: web-dir-route（本地 http 目录挂载）

## Objective

WE 的 `directory` 类型用户属性需要把用户选中的整个目录暴露给页面，当前只能通过
`/_xw/file/<id>` 逐个文件注册，且上限 64。新增目录级只读路由
`/_xw/dir/<id>/<rel>`，为后续 `ondemand` / `fetchall` 两种目录属性模式提供加载能力。

User stories:
- 页面拿到 `http://127.0.0.1:<port>/_xw/dir/0/sub/a.png` 能直接 `<img src>` 加载。
- 页面尝试 `../`、绝对路径、指向外部的符号链接时得到 403，不会读到目录外文件。
- 同一个目录重复挂载返回同一 id；超过上限时明确失败而不是静默截断。

非目标：写入、目录列表（index）、WebDAV、认证；`ondemand`/`fetchall` 语义与事件投递
（后续任务）。

## Tech Stack

C11 + GLib/GIO（`GSocketService`，已有依赖）。路由与现有 `/_xw/file/<id>` 共用同一
连接 worker、Range/HEAD/keep-alive 逻辑；无新依赖。

## Commands

```
Build:      make
Test:       make test          # tests/test_web_http（纯逻辑 + 本地 socket，无 X）
Integ(X11): make test-xvfb
Dep guard:  make check-client-libs
```

## Project Structure

```
src/web_http.c        → 改：XWWebHttp 增 dirs[]；mount_dir / dir_lookup；路由与释放
src/web_http.h        → 改：XW_HTTP_DIR_MAX、xw_web_http_mount_dir 声明
tests/test_web_http.c → 改：挂载/去重/越界/符号链接/未知 id/HEAD 用例
docs/specs/web-dir-route.md → 本文件
```

## Code Style

```c
/* 目录根只在 mount 时 realpath 一次；lookup 在 worker 内 realpath 候选路径，
 * 再做前缀校验（root 本身或 root + '/'），符号链接逃逸同样被挡住。 */
int xw_web_http_mount_dir(XWWebHttp *h, const char *abs_dir);
```

## Testing Strategy

- 单测（`make test`）：真实临时目录（`mkdtemp` 于 `/tmp/opencode`）：
  - 挂载返回稳定 id；同一目录去重；文件/不存在路径/NULL 返回 -1
  - `GET /_xw/dir/<id>/sub/a.txt` → 200 + 内容 + `text/plain`
  - `GET /_xw/dir/<id>/../secret.txt` → 403
  - 目录内指向 `/etc/passwd` 的符号链接 → 403
  - 未知 id → 404；`HEAD` → 200 且无 body
  - 超过 `XW_HTTP_DIR_MAX` → -1
- 集成：本任务不动 WebKit 侧，后续目录属性任务复用该路由。

## Boundaries

- Always: 只读；`realpath` 前缀校验；错误码区分 403（越界）与 404（不存在）；worker
  线程不阻塞主循环。
- Ask first: 提高 `XW_HTTP_DIR_MAX`；允许目录 index 列表。
- Never: 暴露挂载目录的父目录；跟随符号链接出根；对挂载目录做写操作。

## Success Criteria

- [ ] 目录内文件可通过 `/_xw/dir/<id>/<rel>` 读取（含 Range/HEAD）
- [ ] `..`、绝对路径、符号链接逃逸全部 403
- [ ] 重复挂载稳定、超限失败、stop 后无泄漏
- [ ] `make test` / `make check-client-libs` 绿
