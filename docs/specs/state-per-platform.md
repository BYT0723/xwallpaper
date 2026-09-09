# Spec: per-platform state files (X11 vs Wayland)

## Objective

X11（xrandr 名）和 Wayland（wlr 输出名）的 monitor 命名空间不一致，
共用一个 state 文件会导致切平台恢复时贴错屏（同名如 `DP-1` 位置不同）
或回退到旧平台几何（异名如 `HDMI-1` vs `HDMI-A-1`）。
目标：两平台各用各的状态文件，互不污染；X11 用户零感知。

## Decision

- X11 沿用 `$XDG_STATE_HOME/xwallpaper/state`（无迁移、无改名，现有用户不受影响）。
- Wayland 新增 `$XDG_STATE_HOME/xwallpaper/state-wayland`（从空开始，
  旧文件里可能混入 Wayland 测试条目，不做迁移——Wayland 支持本身是新的，
  旧文件按 X11 语义解读）。
- 选择点：daemon 进程内平台唯一（`app->use_wayland`），save/keep/restore/
  pending 四处调用点统一走平台分发，无交叉读写。

## API

```c
/* state.h */
int xw_state_path_for(int use_wayland, char *out, size_t n); /* 1 → state-wayland */
int xw_state_path(char *out, size_t n);                       /* 不变 == _for(0) */
```

```c
/* app.c (static) */
static int app_state_path(XWApp *app, char *out, size_t n);
/* X11-only 构建下无 use_wayland 字段，helper 内 #ifdef 消化差异 */
```

## Testing Strategy

- 单测（`tests/test_state.c`）：`_for(1)` 后缀为 `state-wayland`；
  `_for(0)` 与 `xw_state_path()` 同值；`XDG_STATE_HOME` 未设置时 fallback
  `~/.local/state` 两平台一致。
- 现有 `test_state` / `test_restore`（xvfb，走 X11 路径）必须全绿，
  证明 X11 行为零变化。
- 手动：Wayland 下 set → 查 `state-wayland` 落盘；切回 X11（或反之）
  restore 互不干扰。

## Boundaries

- Always: 四处调用点全部切换，不留直连 `xw_state_path` 的 app 逻辑；
  `make test` 全绿。
- Never: 不改行格式、不碰 `xw_state_save/load/upsert` 语义；
  不删除旧 `state` 文件；不为 Wayland 再按 compositor 细分。

## Success Criteria

1. `set -m DP-1 x.png`（Wayland）只写 `state-wayland`，`state` 不动。
2. 同一 HOME 下 X11 daemon 只读 `state`，Wayland 只读 `state-wayland`。
3. `make test && make test-xvfb && make check-client-libs` 全过。
