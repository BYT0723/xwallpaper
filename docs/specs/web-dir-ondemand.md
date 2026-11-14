# Spec: web-dir-ondemand（directory 属性 ondemand 模式）

## Objective

WE `directory` 类型属性的 `ondemand` 模式通过
`window.wallpaperRequestRandomFileForProperty(name, cb)` 按需取随机文件（幻灯片壁纸）。
xwallpaper 目前只把属性值（目录路径）原样投递，页面调用该函数会因未定义而中断。

User stories:
- 幻灯片壁纸选中目录后，每次调用拿到该目录内一个随机图片的**可加载 URL**（http 源，
  不是 `file://`）。
- 空目录、目录被删、未设置目录时回调收到空串，页面自行回退，不崩溃。
- 未知属性名或非 directory 属性不发回复，不崩。
- 目录内文件按 `fileType` 过滤（image / video），递归子目录，上限内稳定。

非目标：`fetchall` 模式的两个事件（下一个任务）；缩略图/预加载；从页面传入任意路径
（路径只来自 project.json 默认值或 `--set-property`，页面无法指定）。

## Tech Stack

C11 + GLib（`GDir`/`g_uri_escape_string`）；目录枚举为纯逻辑模块，可单测；复用上一任务
的 `/_xw/dir/<id>/<rel>` 路由；无新依赖。

## Commands

```
Build:      make
Test:       make test
Integ(X11): make test-xvfb
Dep guard:  make check-client-libs
Manual:     ./xwallpaper --set-property pics=/path/to/photos   # 再观察幻灯片
```

## Project Structure

```
src/web_dir_scan.[ch]          → 新增：扩展名过滤 + 递归枚举 + 排序（纯逻辑）
src/web_props.[ch]             → 改：解析 mode / fileType
src/web.c                      → 改：shim、xwRandom 消息、目录解析、随机 URL 回复
tests/test_web_dir_scan.c      → 新增
tests/test_web_props.c         → 改：mode/fileType 断言
tests/data/wedirprops/         → 新增 fixture（project.json + 两张图 + 回显页）
tests/integration/test_web.c   → 改：ondemand 集成用例
docs/specs/web-dir-ondemand.md → 本文件
```

## Code Style

```c
/* 路径只来自 project.json / --set-property；页面只能引用属性名。 */
static int web_dir_value_path(WebCtx *ctx, const char *value, char *out,
                              size_t n); /* file:// / 绝对 / 相对 root_dir */
```

## Testing Strategy

- 单测：`tests/test_web_dir_scan.c`（临时目录：图/视频/子目录/符号链接/上限/排序）。
- 单测：`test_web_props` 断言 `mode`/`fileType` 解析。
- 集成：`tests/data/wedirprops` 页面注册回调并把返回值写进 title；断言 URL 形如
  `http://127.0.0.1:<port>/_xw/dir/<id>/...`；把属性改成不存在目录后回调收到空串。
- 路由本身（200/Range/403）已由 `tests/test_web_http.c` 覆盖，不重复。

## Boundaries

- Always: 只读；枚举深度 ≤8、文件数上限 4096；跳过符号链接；无 http 服务器时回空串。
- Ask first: 改属性值语义、允许页面传路径。
- Never: 让页面拿到挂载根之外的路径；同步阻塞主循环做大目录 IO（上限内可接受）。

## Success Criteria

- [ ] `wallpaperRequestRandomFileForProperty` 可用，返回可加载的 http URL
- [ ] 目录为空/不存在/属性未知时安全回空串或无回复
- [ ] image/video 过滤与递归枚举正确，顺序稳定
- [ ] `make test` / `make test-xvfb` / `make check-client-libs` 绿
