# Spec: web-noninteractive (壁纸页去交互化)

## Objective

`--web` 壁纸页不应出现浏览器式交互：`grab` 手形光标、可拖拽元素、文本选中。
保留被动能力：动画/`rAF`/video/canvas、WE `project.json` 投递、`mousemove` 视差、点击特效。
即：禁选中/拖拽/光标形态，不禁点击与悬停。

## Commands

```
Build: make
Test:  make test
Integ: make test-xvfb (重点 tests/integration/test_web)
Guard: make check-client-libs
```

## Project Structure

```
src/web.c                    → 改：web_attach_view() 注入 user stylesheet + user script
tests/integration/test_web.c → 扩展：断言 user-select:none / cursor:default / dragstart/selectstart 被 preventDefault
docs/specs/web-noninteractive.md → 本文件
```

## Code Style

```c
static const char *kWallpaperCSS = "* { ... user-select:none !important; ... cursor:default !important; }";
WebKitUserContentManager *ucm = webkit_web_view_get_user_content_manager(...);
webkit_user_content_manager_add_style_sheet(ucm, sheet); ...
```

- 命名沿用 `xw_<module>_<action>`；C11；无晦涩 X11 trick 不加注释。
- X11/Wayland 共用 `web_attach_view()`，只改一处。

## Testing Strategy

- 集成（Xvfb）：`load_html` 含 `draggable` + `cursor:grab` 元素，JS 取 computed style
  断言 `user-select==none`、`cursor==default`（USER 级 `!important` 覆盖作者样式），
  合成 `dragstart`/`selectstart` 事件断言 `defaultPrevented==true`。
- 现有用例（paint/menu/storage/WE）必须全过：证明动画、存储、WE 投递未被破坏。
- `make test` + `make check-client-libs` 回归。

## Boundaries

- Always: 只在 `web_attach_view` 加注入；`destroy` 无新增资源；CLI 零 heavy 依赖。
- Ask first: 全穿透（Shape 置空输入区、禁 wheel/click）——本次不做。
- Never: 拦截 click/wheel/mousemove；改 protocol；链 webkit 进 CLI。

## Success Criteria

1. 含可拖拽/`cursor:grab` 的测试页在壁纸 view 里 computed `cursor==default`、`user-select==none`。
2. 合成 `dragstart`/`selectstart` 均被 `preventDefault`。
3. 右键菜单仍被屏蔽；页面仍能渲染（蓝色像素用例过）；WE 属性投递用例过。
4. `make test`、`make test-xvfb`、`make check-client-libs` 全过。
