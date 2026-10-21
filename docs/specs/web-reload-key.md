# Spec: web 壁纸按键刷新（Ctrl+R）

## Objective

web 壁纸人物大小/布局错乱时，用户按 `Ctrl+R` 刷新当前 web 壁纸，
语义等同浏览器刷新（`webkit_web_view_reload`），重排重绘页面。
只走 `wallpaper.keys` 按键路径，不新增 CLI 子命令，不改协议。

用户已确认：

- 只加按键（不要 CLI `reload` 命令）
- 写法 `Ctrl+R`（浏览器一致）
- 仅 web 生效，其它后端静默忽略

## Tech Stack

C11，现有 `keybinds` / `app dispatch` / `web control` 链路。
无新依赖。CLI 保持 thin（不链 webkit），`make check-client-libs` 必须绿。

## Commands

```sh
make test               # 单元：test_keybinds 等
make test-xvfb           # 集成：Xvfb 下按键/后端
make check-client-libs  # 回归：CLI 不链重型库
```

## Project Structure

```
src/keybinds.c/.h   新增 XW_ACT_RELOAD，解析 `reload`，to_action 映射 reload
src/app.c           dispatch_key 支持 Ctrl 修饰键（看 ev->xkey.state）
src/web.c           web_control 支持 name="reload" → webkit_web_view_reload
src/backend.h       control 注释补 reload（可选，一行）
tests/test_keybinds.c   解析/to_action 新用例
README.md           keys 示例补一行（可选，用户确认后）
docs/specs/web-reload-key.md  本文件
```

按键文件写法（规范拼写，大写 C）：

```
Ctrl+r reload
```

## Code Style

沿用仓库风格（C11、`xw_<module>_<action>`、无冗余注释）：

```c
if (strcmp(name, "reload") == 0) {
  if (!ctx || !ctx->view)
    return -1;
  webkit_web_view_reload(WEBKIT_WEB_VIEW(ctx->view));
  return 0;
}
```

## Testing Strategy

- 单元（`make test`）：`test_keybinds` 新增
  `Ctrl+r reload` 解析为 `XW_ACT_RELOAD`；`to_action` 得 `reload/""`；
  未知动作仍跳过；旧 9 行真实文件仍解析为 9 条（回归）。
- 集成/手动（X11 真机或 Xvfb）：聚焦 web 壁纸按 `Ctrl+R` 页面重载；
  按 `r`（无 Ctrl）不触发；video/scene 上按 `Ctrl+R` 静默无错。
- 门禁：`make test` + `make check-client-libs` 全绿；
  `test-xvfb` 有 Xvfb 时跑，无则注明跳过。

## Boundaries

- Always: 协议向后兼容（本次不动协议）；按键不支持的后端静默 no-op；
  薄 CLI 不链 webkit/mpv/imlib2。
- Ask first: 若要再加 `xwallpaper reload` CLI 或 F5 别名，需另行确认。
- Never: 对 root 做 XGrabKey；把 reload 写进 state；引入新依赖；
  在 `main` 上直接开发（本次已开 `feat/web-reload-key`）。

## Success Criteria

1. `wallpaper.keys` 加一行 `Ctrl+r reload` 后，聚焦 web 壁纸按 `Ctrl+R` 触发
   `webkit_web_view_reload`，人物大小/布局重排。
2. 不加修饰单独按 `r` 不触发刷新（仍走 rotate 或无绑定）。
3. video/scene/image 上按 `Ctrl+R` 不报错、不影响其它窗口。
4. 无 `--keybinds` 时默认路径 `~/.config/xwallpaper/keys` 同样生效。
5. `make test` 与 `make check-client-libs` 全绿。

## Open Questions

- 规范拼写定为 `Ctrl+r`（`xw_keybinds_norm_key` 保持原样，`Ctrl` 大写 C）。
  文件里写成 `ctrl+r`（全小写）是否也要接受？默认不接受，保持大小写敏感，
  与现有 `Super+l` 一致。——如需大小写不敏感请在此确认。
- Wayland 下按键派发本来就未做（见 wallpaper-control spec），本次同样只做 X11，
  Wayland 用户仍走以后可能的 CLI 路径。——确认接受。

## 关键技术发现（实现前已确认）

- `src/app.c dispatch_key` 当前只用 `XKeysymToString`，忽略
  `ev->xkey.state`，所以 `Ctrl+r` 今天永远命中不了 `r` 以外的键；
  本次必须补上 `ControlMask → "Ctrl+"` 前缀构造，再查表。
- `src/keybinds.c parse_line` 当前只认 seek/add volume/cycle mute/cycle
  pause/cycle_values video-rotate，需加 `reload`（无参数，一个 token）。
- `src/web.c web_control` 当前支持 volume/mute/pause/seek，需加 reload
  分支调 `webkit_web_view_reload`（与 crash 自愈同一调用，保留 overrides）。
