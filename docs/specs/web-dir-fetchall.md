# Spec: web-dir-fetchall（directory 属性 fetchall 事件）

## Objective

WE `directory` 属性的 `fetchall` 模式通过 `wallpaperPropertyListener` 的两个事件把
目录内容整体交给页面：

- `userDirectoryFilesAddedOrChanged(propertyName, changedFiles)`
- `userDirectoryFilesRemoved(propertyName, removedFiles)`

用于图片/视频批量导入的幻灯片或网格壁纸。当前 xwallpaper 只投递 `applyUserProperties`，
页面收不到文件列表，功能不可用。

User stories:
- 壁纸加载后立即收到该属性的全部文件（added = 全部），文件是可直接加载的 http URL。
- 我用 `--set-property pics=<另一个目录>` 后，页面收到正确的 removed + added 差异。
- 我把目录改成不存在/空目录后只收到 removed，页面不留死链，不崩。
- 同一文件未变化时不会重复推送（导航后除外，新页面需要全量）。

非目标：`ondemand` 模式（上一任务）；文件内容变更监听（inotify）；跨属性共享文件列表。

## Tech Stack

C11 + GLib；复用 `web_dir_scan` 枚举与 `/_xw/dir/<id>/<rel>` 路由；差异计算为纯逻辑，
可单测；无新依赖。

## Commands

```
Build:      make
Test:       make test
Integ(X11): make test-xvfb
Dep guard:  make check-client-libs
Manual:     ./xwallpaper --set-property pics=/path/to/photos
```

## Project Structure

```
src/web_dir_scan.[ch]          → 改：xw_web_dir_diff 纯逻辑
src/web.c                      → 改：per-property 状态、事件投递、触发点、导航重置
tests/test_web_dir_scan.c      → 改：diff 用例
tests/data/wedirfetch/         → 新增 fixture（project.json + 图 + 事件回显页）
tests/integration/test_web.c   → 改：fetchall 集成用例
docs/specs/web-dir-fetchall.md → 本文件
```

## Code Style

```c
/* 两边都是已排序的绝对路径数组；输出 malloc 的 added/removed 字符串数组。 */
int xw_web_dir_diff(char *const *prev, int np, char *const *cur, int nc,
                    char ***added, int *na, char ***removed, int *nr);
```

## Testing Strategy

- 单测：diff 的增/删/不变/空集合/NULL 边界；排序输入。
- 集成：fixture 页面累计事件到 `window.__ev`：
  - 加载 → `A:pics:2`
  - 改成含 1 个文件的目录 → `R:pics:2` + `A:pics:1`
  - 改成不存在目录 → `R:pics:1`
  - 首个 URL 以 `http://127.0.0.1:` 开头且含 `/_xw/dir/`
- 路由/枚举本身已由前两个任务覆盖。

## Boundaries

- Always: 只对 `mode=="fetchall"` 的属性发事件；文件上限 4096；页面回调缺失时短暂重试
  后放弃；导航/换壁纸清空状态。
- Ask first: 文件数上限、inotify 实时监听。
- Never: 让页面提交任意路径；阻塞主循环做大目录 IO（上限内可接受）。

## Success Criteria

- [ ] 加载后 added 为全部文件，URL 可加载
- [ ] 属性变化后 added/removed 差异正确，顺序稳定
- [ ] 空/不存在目录只发 removed，不崩
- [ ] 导航后重新全量；ondemand 属性不发事件
- [ ] `make test` / `make test-xvfb` / `make check-client-libs` 绿
