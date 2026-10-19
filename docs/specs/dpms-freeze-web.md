# Spec: DPMS 熄屏冻结 web

## Objective
`xset dpms force off` 后 web 壁纸不再点亮显示器、不出声、CPU 降载；
DPMS 恢复后自动恢复。对齐现有 `video`/`scene` 的 `set_paused` 行为。

用户确认：页面任意 JS 逻辑能停则停，停不彻底则至少暂停媒体 + 静音。

## 现状
- `backend.h:101 set_paused`：DPMS 熄屏暂停钩子，`app.c:1080 apply_dpms_to_backend` 在
  `power != DPMSModeOn` 时调用，仅 X11（Wayland 直接 return）。
- `video.c:157` 已实现（mpv pause）；`scene.c:811` 已实现（`paused + tick` 跳过 + 静音）。
- `web.c:1035` 无 `set_paused/.tick`，DPMS off 时 WebKit 继续呈现 + 媒体/音频继续 +
  `xw_web_we_tick` 继续 `evaluate_javascript` 下发音频数据。
- WebKit2GTK 4.1（本机 2.54.1）无 page-suspend 公开 API（头文件仅 `set_is_muted`），
  任意 JS 逻辑无法彻底冻结，只能靠隐藏页面触发 WebKit 隐藏节流 + 主动暂停媒体。

## 行为
1. `WebCtx` 新增 `dpms_paused`（与现有 `ctl_paused` 用户手动暂停独立，任一生效）。
2. 新增 `web_set_paused(b, paused)` 并注册到 backend：
   - `paused=1`：`gtk_widget_hide(window)` 停呈现；`webkit_web_view_set_is_muted(TRUE)`；
     `__xwCtlMedia('pause')` 暂停页面音视频；`xw_web_we_tick` 跳过该 view 的音频下发。
   - `paused=0`：`gtk_widget_show(window)` 恢复呈现；`set_is_muted` 回到 `ctl_muted`；
     仅当 `ctl_paused==0` 时才 `__xwCtlMedia('play')`（不清除用户手动暂停）。
3. 新建/切换壁纸时走现有 `apply_dpms_to_backend`，熄屏期间建的 web 直接进冻结态。
4. Wayland：本次不动（`xw_app_dpms_poll` 在 Wayland 本就 no-op，等 `ext-idle-notify`）。

## 不做
- 不杀 WebProcess 重载（状态丢失、代价大）；JS 逻辑靠隐藏节流，不保证归零。
- 不改 `video`/`scene` 现有单标记行为（其 DPMS/手动冲突是已知问题，本次不动）。
- 不碰 CLI 协议（无新增命令）。

## Commands
```
make
make test
make test-xvfb
make check-client-libs
```

## Testing Strategy
- 单元：`web_set_paused` 标记语义（dpms/user 独立、恢复不清除用户暂停）。
- 集成（xvfb + DPMS）：DPMS off 时 web 窗口隐藏且无帧呈现、媒体暂停、静音；
  DPMS on 恢复呈现；用户 pause + DPMS 交错场景。
- 手动：`xwallpaper set --web page.html` 后 `xset dpms force off/on` 观察。

## Success Criteria
- [ ] DPMS off 后 web 无呈现、无声、`we_tick` 不再向其 `evaluate_javascript`。
- [ ] DPMS on 后自动恢复；用户手动 pause 在 DPMS 往返后保持。
- [ ] `make test` + `make test-xvfb` + `make check-client-libs` 全过。

## Open Questions
- 隐藏用 `gtk_widget_hide/show` 是否足够，或需 `XUnmapWindow` 兜底（实现时验证）？
