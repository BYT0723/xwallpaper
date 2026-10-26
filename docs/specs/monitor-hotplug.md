# Spec: Monitor 热插拔自适应 (monitor-hotplug)

## Objective

monitor 布局变化 (插拔显示器、`xrandr --output … --off/--auto`、分辨率/位置变化、
primary 切换) 时，`xwallpaperd` 无需重启自动贴合新布局。

用户故事:
- 作为多屏用户，我热插拔一台显示器后，已有壁纸自动移到新几何上，不错位、不黑屏。
- 作为 `xwallpaper set -m DP-1 …` 用户，DP-1 被拔掉后对应窗口被清理，不残留僵尸窗口；
  重插后用原来的 `set` / `restore` 即可恢复，不需要重启 daemon。
- 作为 `-g … --name <…>` 窗口 (Screen 全屏 / `grp_*` 组并集) 用户，
  布局变化时 Screen 自动贴合新全屏包围盒，`grp_*` 保持不动，
  野名 (如历史遗留的 screen/landscape) 被清理，避免画面混乱。
- 作为 `clear --keep` 留过 last 的用户，重插显示器后 daemon 自动恢复该屏的
  last 壁纸 (仅 `keep=1` 记忆；无记忆的新屏保持空白，不自动建窗口)。

非目标: Wayland (wlr-layer-shell 由 compositor 重排，本次不动)。

## ASSUMPTIONS

1. 只做 X11 路径 (`src/x11.c` / `src/app.c` / `src/daemon.c`)，Wayland 不动。
2. XRandR 事件可靠：root 上选 `RRScreenChangeNotifyMask | RRCrtcChangeNotifyMask | RROutputChangeNotifyMask` 即可收到全部布局变化。
3. `mpv --wid` 窗口 resize 后 mpv 自动跟随新尺寸，无需额外通知 (待真机验证，spec 先按此假设，验证失败则补 `video_on_event`)。
4. 去抖窗口 500ms 足够覆盖一次 `xrandr` 操作产生的事件 burst。

## Commands

```sh
Build:      make
Run tests:  make test
Xvfb tests: make test-xvfb
Check deps: make check-client-libs   # CLI 必须只链 libc
Debug:      XW_DEBUG_DAEMON=1 ./xwallpaperd   # 打印 randr 事件与 reconfigure 动作
Manual smoke (live X):
  ./xwallpaperd &
  ./xwallpaper set -m DP-1 tests/data/red.png
  xrandr --output DP-1 --off          # 期望: DP-1 窗口自动销毁
  xrandr --output DP-1 --auto         # 期望: 仍无窗口 (不自动建)，手动 set 恢复
  xrandr --output DP-1 --mode 1920x1080 --pos 1920x0
                                     # 期望: 已有窗口 XMoveResize 到新 rect 并重绘
```

## Project Structure

```
src/x11.c/.h   RandR 订阅 + 几何查询 (select_randr_input / resolve_output /
               screen_bbox / connected_outputs)
src/app.c/.h   xw_app_reconfigure(): 输出窗口重贴/断开销毁；-g 按名跟随
               (输出同名贴新几何 / Screen 贴全屏包围盒 / grp_* 保持 / 野名删除) + save_state
src/daemon.c   poll 循环中收 RandR 事件 → 去抖 → 调 reconfigure
src/image.c    已有 ConfigureNotify 重绘 (复用，无需改)
src/web.c      子窗口随 ConfigureNotify 跟随 MoveResize (需补)
src/scene.c    ConfigureNotify 更新视口/重绘 (需补，若验证发现错位)
src/video.c    按假设无需改 (验证失败才补 on_event)
docs/specs/monitor-hotplug.md  本文件
```

## Code Style

沿用仓库风格 (C11、`xw_<module>_<action>`、无冗余注释)，例如：

```c
int xw_app_reconfigure(XWApp *app, XWError *err) {
  for (int i = app->nwins - 1; i >= 0; i--) {
    if (app->wins[i]->is_geometry) {
      remove_window(app, i);   /* -g 绝对坐标已失效，直接移除 */
      continue;
    }
    int x, y; unsigned int w, h;
    if (xrandr_lookup(app->dpy, root, app->wins[i]->output, &x, &y, &w, &h) != 0) {
      remove_window(app, i);   /* 输出已断开 */
      continue;
    }
    if (x != app->wins[i]->x || y != app->wins[i]->y ||
        w != app->wins[i]->width || h != app->wins[i]->height) {
      XMoveResizeWindow(app->dpy, app->wins[i]->win, x, y, w, h);
      app->wins[i]->x = x; app->wins[i]->y = y;
      app->wins[i]->width = w; app->wins[i]->height = h;
    }
  }
  return xw_app_save_state(app);
}
```

## Testing Strategy

- 单元 (`make test`，无 X)：把 reconcile 提成纯函数
  `xw_reconcile(old_wins, new_outputs) -> {move, destroy}`，断言：
  位移/缩放/断开/新增(忽略)/`-g` 全清 / primary 切换改名义。
- 集成 (`make test-xvfb`)：Xvfb 下起 daemon + image 壁纸，
  发合成 `RRScreenChangeNotify` / 调 `XRRSetCrtcConfig` 改几何，
  断言窗口几何跟随、`--list` 一致、无 crash。
- 手动冒烟 (live X)：按 Commands 里的 xrandr 序列走一遍，
  image / video / web / scene 各验一次 resize 重绘正确。
- 门禁顺序：`make test` → `make test-xvfb` → `make check-client-libs`。

## Boundaries

- Always: 跑三门禁；协议向后兼容 (本次只新增 daemon 内部调用，不改协议)；
  reconfigure 后回写 state；RandR 事件去抖，避免 burst 重排。
- Ask first: 新增 `xwallpaper refresh` 手动命令 (本次默认不加，如自动监听在某些 WM 不可靠再补)；
  改动 Wayland 路径；新依赖。
- Never: CLI 链 webkit2gtk/mpv/imlib2；fork 外部 player；对 root 做 XGrab；
  无记忆的新输出自动建窗口 (有 keep=1 last 的自动恢复，无记忆的保持空白)。

## Success Criteria

1. 分辨率/位置变化：对应输出窗口 `XMoveResize` 到新 rect，image 重绘铺满，
   video 继续播 (无黑屏)，web/scene 无错位拉伸。
2. 输出断开：该输出窗口被销毁，`--list` 不再含它，daemon 不退出；
   其它屏壁纸不受影响。
3. 输出新增：无 `keep=1` 记忆时不自动建窗口，已有窗口不动；
   有记忆时自动恢复该屏的 last。
4. `-g` 窗口：`Screen` 自动贴合新全屏包围盒；`grp_*` 保持不动；
   野名 (如历史遗留的 screen/landscape) 被删除。
5. burst 操作 (一次接多台/连续 xrandr) 只触发一次有效重排，无闪烁循环。
6. 三门禁全绿。

## Open Questions

1. `mpv --wid` 在 `XMoveResizeWindow` 后是否真自动跟随？需真机确认；
   若有黑边/不跟随，补 `video_on_event` (ConfigureNotify → `mpv_set_property wid` 重设或发 `window-scale` 更新)。
2. WebKit 子窗口 (GtkPlug reparent) 是否能收到父窗口的 ConfigureNotify 并跟随？
   若不能，在 `web_on_event` 里对 child 做 `XMoveResizeWindow`。
3. Scene 的 GL 视口 (`xw_gl_create` 时宽高) resize 后是否重建？
   若错位，在 `scene_on_event` 补 viewport 更新 + 强制重绘一帧。
4. 部分 WM 可能不透传 RandR 到 override-redirect 窗口的 client：
   若自动监听在目标 WM 上收不到事件，再补 `xwallpaper refresh` 手动兜底 (本次先不做)。
