# Spec: 按键操作窗口内信息提示（key OSD）

## Objective

当前按键操作（`wallpaper.keys`：音量/静音/暂停/快进/旋转/重载）与 CLI 直控
（`xwallpaper volume/mute`）生效时，窗口内没有任何视觉反馈，用户无法确认
操作是否生效、当前值是多少。

目标：所有可控后端（video / web / scene）在按键与 CLI 控制成功后，在窗口内
左下角显示一行 mpv 风格的短暂文本提示（约 1.5–2s 自动消失），内容与动作一致：

- `add volume` → `Volume: 62%`
- `cycle mute` → `Mute: on` / `Mute: off`
- `cycle pause` → `Paused` / `Playing`
- `seek ±N` → `Seek +5s` / `Seek -5s`
- `cycle_values video-rotate` → `Rotate: 90°`（video 仅有）
- `reload` → `Reloaded`（web 仅有）

用户故事：
- 作为 video 壁纸用户，按 `0/9/-/=` 能看到音量/静音/暂停的即时提示。
- 作为 web/scene 壁纸用户，按同样的键能看到与 video 一致风格的提示。
- 作为多屏用户，只有被操作的那个窗口显示提示，不打扰其它屏。

非目标：中央大图标、常驻 HUD、持久化音量到 state、可配置开关（首版常开，
后续如需再加 `--no-osd`）。

## Tech Stack

- C11，现有后端：libmpv（video）、webkit2gtk（web）、桌面 GL + freetype（scene）
- 纯逻辑模块不链 X11/GTK/mpv（沿用 `keybinds.c` 做法，可单测）

## Commands

```
Build:      make
Test:       make test
Integration: make test-xvfb
Dep guard:  make check-client-libs
Manual smoke (X11):
  ./xwallpaperd &
  ./xwallpaper set --video tests/data/bounce.mp4 --keybinds <keys>
  # 聚焦壁纸窗口按 0/9/-/=/l/h，左下角应出现对应提示
  ./xwallpaper volume +2     # CLI 路径同样应出现提示
  ./xwallpaper clear; ./xwallpaper exit
```

## Project Structure

```
src/osd.c/.h          新增：纯逻辑，格式化提示文本 + 超时常量
                       xw_osd_format(name, value, extra, out, n)
src/video.c           video OSD：mpv show-text（osd-level 0→1）
src/web.c             web OSD：JS toast div __xwOSD（复用 evaluate 通道）
src/scene.c           scene OSD：GL 文本覆盖层（复用 freetype 光栅化，tick 过期）
src/scene_text*.c     scene OSD 绘制复用（若需新增 helper 放这里）
src/app.c             触发点：dispatch_key + xw_app_control 成功后调 backend OSD
src/backend.h         新增可选回调 show/osd（暂定 int (*osd)(XWBackend*, const char*)）
tests/test_osd.c      新增：格式化单测（纯逻辑，无 X）
tests/integration/    扩展：各后端 control 后 OSD 被调用（mock/属性回读 + JS 无错）
docs/specs/key-osd-feedback.md  本文件
```

备选（实现时二选一，需在 plan 阶段锁定）：
- A. 每个后端 `control()` 内自己显示（video/web/scene 各知最终值，调用方零改动）。
- B. `control()` 返回最终文本，`app.c` 统一调用 `b->osd(text)`。
- 推荐 A（改动最小，key 与 CLI 同走 `control`，天然全覆盖）。

## Code Style

沿用仓库风格（C11、`xw_<module>_<action>`、无冗余注释）：

```c
/* osd.c — 纯逻辑，无 X11/GTK/mpv 依赖 */
int xw_osd_format(const char *name, const char *value, int extra,
                  char *out, size_t n);
```

```c
/* video control 成功后（示例 A 方案） */
{
  char msg[64];
  if (xw_osd_format(name, value, cur, msg, sizeof(msg)) == 0) {
    const char *args[] = {"show-text", msg, "1500", NULL};
    mpv_command(m->mpv, args);
  }
}
```

## Testing Strategy

- **单元（`make test`）**：`tests/test_osd.c`
  - volume/mute/pause/seek/rotate/reload 文案逐项断言（含边界：钳位 0/100、
    空 value、未知 name 返回 -1 不显示）。
  - 纯逻辑，不链重库（`test_osd → src/osd.o`，Makefile 显式规则）。
- **集成（`make test-xvfb`）**：
  - video：control volume 后 mpv 收到 `show-text`（读属性或 mock；撤掉实现变红）。
  - web：control 后 evaluate 的 JS 含 `__xwOSD` 且无 JS 错误。
  - scene：control 后 OSD 状态置位，tick 2s 后清除（逐帧断言或标志位）。
- **手动冒烟**：真机三后端逐键确认提示出现且 2s 内消失；多屏只提示目标窗口；
  image 上按键/CLI 无提示且行为不变（按键静默、CLI 报错）。

覆盖率期望：纯逻辑 `osd.c` ~100%；后端 OSD 行经冒烟 + Xvfb（crash 级），
视觉像素级为人工确认。

## Boundaries

- Always: `make test` → `make test-xvfb` → `make check-client-libs` 全绿；
  `osd.c` 不链重库；协议不动（OSD 不走 socket）；不支持的动作无提示
  （按键静默 no-op，CLI 报错路径不提示）。
- Ask first: 改 `osd-level` 全局值以外的 mpv OSD 样式（字体/位置/时长）；
  给 web 注入除 toast div 外的脚本；新增 `--no-osd` 开关或持久化。
- Never: 对 root 做 XGrabKey；引入新依赖（Xft 以外的 overlay 库）；
  把音量写进 state 文件（本次不做持久化，沿用 wallpaper-control 决策）；
  在 `xwallpaper` CLI 二进制中链接 webkit/mpv/imlib2。

## Success Criteria

1. 聚焦壁纸窗口按键（`0/9/-/=/l/h/r/Ctrl+r`）成功时，video / web / scene
   窗口左下角出现对应文本提示，约 2s 自动消失。
2. `xwallpaper volume/mute`（含 `-m/--all`）同样触发提示；`--all` 时每个
   成功窗口各自提示。
3. 不支持的动作无提示：image 上按键静默、CLI 报错且无提示；scene 上
   seek/rotate 无提示。
4. 多屏只有目标窗口提示；其它屏不受影响。
5. `make test`、`make test-xvfb`、`make check-client-libs` 全绿；
   README「Key bindings」章节增补 OSD 说明一行。
6. Wayland：video（libmpv render）与 web（DOM toast）同样提示；
   scene 在 Wayland 上不支持，无需提示。

## Open Questions（已确认 2026-10-27）

- [x] mpv `show-text` 在 `osd-level=0` 下被抑制 → 改为 `1`，用户确认 OK。
- [x] scene OSD 字体：用系统字体（DejaVu Sans / freetype 系统路径），
      不复用场景内嵌字体链路。
- [x] 首版常开，无 `--no-osd` 开关，用户确认 OK。

## ASSUMPTIONS I'M MAKING

1. X11 与 Wayland 都要提示（video/web 双平台，scene 仅 X11）。
2. mpv 风格左下角短暂文本即可，不做中央大图标。
3. 覆盖 volume/mute/pause/seek（+ video rotate、web reload），image 无提示。
4. CLI 直控与按键走同一 `control` 路径，一并提示。
5. 首版常开，不加开关；文案为英文（与 mpv OSD 一致），后续再议本地化。
→ 如有误请纠正，否则按此实现。
