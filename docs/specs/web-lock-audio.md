# Spec: 锁屏/DPMS 恢复不重播 web 媒体

## Objective

锁屏（SIGSTOP）或 DPMS 熄屏后恢复时，web 壁纸不能把页面里所有
`audio/video` 元素一次性 `play()` 造成“批量出声”；同时锁屏期间页面
应当静音而不是暂停（暂停会在恢复时重播一次性音效）。

## 现状与根因

- `lock.sh` 用 `pkill -STOP -x xwallpaperd` 只冻结 daemon 主进程；
  WebKitGTK 的 `WebKitWebProcess` 是独立子进程，JS/定时器/媒体继续运行。
- `web.c` 的 `__xwCtlMedia('play')` 遍历 DOM + WeakRef 追踪的所有媒体元素
  无条件 `play()`；DPMS 恢复和手动 `pause off` 都走这里 → 批量重播。
- `mute` 会持久化到 state 文件，锁屏脚本直接 `xwallpaper mute on/off`
  会覆盖用户原有的静音设置。

## 行为

1. **选择性恢复**：`__xwCtlMedia('pause')` 只给暂停前确实在播
   （`!paused && !ended`）的元素打 `__xwWasPlaying` 标记；
   `__xwCtlMedia('play')` 只恢复带标记的元素，并清除标记。
   DPMS 暂停/恢复、手动 `pause` 共用这一语义。
2. **transient quiet 控制**：web backend 新增 `quiet` 控制（`on|off|toggle`），
   只置 `WebCtx.ctl_quiet` 并调用 `webkit_web_view_set_is_muted`，
   不暂停媒体、不写 state、不影响 `ctl_muted`/`ctl_paused`。
   `web_sync_view_mute` 的静音条件变为
   `dpms_paused || ctl_quiet || (ctl_started && ctl_muted)`。
3. **锁屏脚本**：`_lock_before` 先执行 `xwallpaper quiet on --all`，再
   递归 `SIGSTOP` `xwallpaperd` 及其全部后代（`WebKitWebProcess` /
   `WebKitNetworkProcess` 等，实测均为直接子进程，仍按递归处理）；
   `_lock_after` 先递归 `SIGCONT`，再执行 `xwallpaper quiet off --all`。
   只按 PID 树发信号，绝不按 PGID（daemon 可能与登录 shell 同组）。
   daemon 未运行/旧版本时静默失败。

## 不做

- 不改 `video` 后端；libmpv 在 daemon 进程内，SIGSTOP 已一并冻结。
- 不做“页面在冻结期间自己 pause 的元素”区分；恢复时最多多播一次，
  代价可接受。
- 不把 `quiet` 持久化到 state；锁屏是临时状态。

## 测试策略

- 单元 `tests/test_cli.c`：`quiet on|off|toggle` 解析为 `XW_CMD_CONTROL`，
  `ctl_name=quiet`，target 与 mute 一致；非法值拒绝。
- 集成 `tests/integration/test_web_dpms.c`：
  - 页面两个 mock 媒体元素，只有 A 在播；DPMS 往返后 A 恢复、B 不播。
  - `quiet on` 只静音、不暂停媒体、不触发 `play`；`quiet off` 不重播。
- 回归：`make test`、`make test-xvfb`、`make check-client-libs`。

## Commands

```
make
make test
make test-xvfb
make check-client-libs
bash -n ~/.dwm/tools/lock.sh
```

## Open Questions

- 长期是否用 cgroup/systemd freeze 替换递归 `SIGSTOP`？当前 daemon 由
  `~/.dwm/autostart.sh` 启动，没有独立 unit，递归 PID 树是唯一可用方案。
- 若 daemon 在被冻结期间被外部 SIGKILL，WebKit 子进程会被 reparent 并
  保持停止态。当前锁屏流程不会杀 daemon，风险可接受；后续可改为记录
  PID 列表或使用 cgroup 原子冻结。
