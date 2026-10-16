# Spec: 壁纸按键控制（wallpaper.keys 全局化）

## Objective

让 `wallpaper.keys`（mpv `input.conf` 风格）在**所有后端**上生效，而不再只依赖 mpv。

**现状问题**：
- `src/` 里**没有任何按键处理**（无 `KeyPressMask`/`XGrabKey`），唯一来源是 mpv 的
  `input-conf`，它要求窗口有焦点；因此只有 video 能用，web/scene 完全无法控制；
- web 后端也没有任何音量/静音手段。

**目标模型（用户确认）**：
- **目标 = 当前聚焦的壁纸窗口**。多屏时即"指针所指/被点到的那个屏"，
  无歧义，且与用户现有体感一致；无需改 dwm 配置。
- **守护进程接管按键**：统一解析按键文件并派发到该窗口的后端；mpv 不再接
  `input-conf`（避免同一个键被处理两次）。
- **完整覆盖 `wallpaper.keys` 里已有的动作**（不因接管而退化）。

## Commands

```sh
# 设置壁纸时指定按键文件（现有 flag，语义从"喂给 mpv"改为"守护进程自己解析"）
xwallpaper set --scene --keybinds ~/.config/dwm/wallpaper.keys <path>

# 未指定时的默认查找顺序
#   $XDG_CONFIG_HOME/xwallpaper/keys  →  ~/.config/xwallpaper/keys

# 仍保留 CLI 直控，供脚本/多屏显式指定（本次一并提供）
xwallpaper volume +2 | -2 | 50
xwallpaper mute [on|off|toggle]
xwallpaper -m DP-1 volume +2
xwallpaper --all mute
```

按键文件语法（与 mpv `input.conf` 兼容的子集，用户现有文件无需修改）：

```
l seek  5
h seek -5
k seek  30
j seek -30
9 add volume -2
0 add volume 2
- cycle mute
= cycle pause
r cycle_values video-rotate 90 180 270 0
```

## 动作 → 后端能力矩阵

| 动作（文件写法） | video (mpv) | web (WebKitGTK) | scene (SDL2_mixer) | image |
| --- | --- | --- | --- | --- |
| `add volume ±N` | `mpv_set_property volume` | JS shim（见下） | 全局音量 | 不支持 |
| `cycle mute` | `mpv_set_property mute` | JS shim `muted` | 全局静音 | 不支持 |
| `cycle pause` | `mpv_set_property pause` | JS：暂停/恢复页面媒体 | `set_paused`（停/续动画） | 不支持 |
| `seek ±N` | `mpv seek` | JS：对 campaign 媒体改 `currentTime` | 不支持 | 不支持 |
| `cycle_values video-rotate ...` | `mpv_set_property video-rotate` | 不支持（无意义） | 不支持 | 不支持 |

不支持的动作对该窗口是**静默 no-op**（按键不该因为后端不支持而报错刷屏），
但 CLI 直控会返回非 0 并说明原因。

## 接口设计

### 按键解析（新增 `src/keybinds.c/.h`）

纯逻辑，便于单测：

```c
typedef enum { XW_ACT_VOLUME, XW_ACT_MUTE, XW_ACT_PAUSE, XW_ACT_SEEK, XW_ACT_ROTATE } XWActKind;
typedef struct { int delta; } XWActVolume;      /* add volume ±N */
typedef struct { int cycle; } XWActMute;        /* cycle mute / cycle pause */
typedef struct { double secs; } XWActSeek;
typedef struct { int values[8]; int n; } XWActRotate;

typedef struct {
  char key[32];        /* 归一化后的键名，如 "l" "9" "-" */
  XWActKind kind;
  union { ... } u;
} XWBind;

int xw_keybinds_parse(const char *text, XWBind *out, int max, XWError *err);
int xw_keybinds_match(const XWBind *binds, int n, const char *key, ...);
```

键名归一化：mpv 小写单字符直接匹配；`Shift`/`Ctrl`/`Alt`/`Super` 前缀与
`XF86Audio*` 明确支持（用户文件里用不到，但避免将来再改协议）。

### 后端接口（`src/backend.h`）

```c
/* 可选的运行时控制，键位派发与 CLI 直控共用。
 * name ∈ {"volume","mute","pause","seek","rotate"}；value 为对应参数（可空）。
 * 返回 0 成功；-1 = 该后端不支持该动作。 */
int (*control)(XWBackend *b, const char *name, const char *value, XWError *err);
```

### 按键的事件来源（`src/x11.c` / `src/app.c`）

- 壁纸窗口的 `event_mask` 增加 `KeyPressMask`（当前只有 Structure/Exposure/
  Enter/Leave/PointerMotion），这样焦点在该窗口时按键会送到守护进程；
- `app.c` 的事件循环里把 `KeyPress` 映射成键名 → 在窗口自己的 binds 表里查 →
  调用该窗口后端的 `control`；
- 焦点语义沿用现状（用户体感已验证可用），本 spec 不引入全局抓键。

### web 的音量与暂停怎么控（主要技术风险）

WebKitGTK 没有音量 API，只能注入 JS。用户壁纸实测混用两种机制：

| 壁纸 | 机制 | 改元素 `.volume` 是否有效 |
| --- | --- | --- |
| 3814714349 | `new Audio(...)` | ✅ |
| 3812349759 | `<audio>` 元素 | ✅ |
| 3768853831 / 3807744786 / 3810650535 | `AudioContext`（Web Audio） | ❌ 需插 GainNode |

shim 要求：
1. 维护 master `volume`（0..1）与 `muted`；
2. 套用到现有与**将来出现**的 `<audio>`/`<video>`（`MutationObserver` +
   `HTMLMediaElement.play` 钩子）；
3. Web Audio：拦截 `AudioNode.prototype.connect`，目标为 `ctx.destination`
   时改接 master `GainNode`；
4. 页面重载后 shim 随用户脚本重新注入（与现有 `wallpaperRegisterAudioListener`
   shim 同一通道），并重新套用已保存的 master 值；
5. 暂停/seek 作用于页面内媒体元素；
6. 守护进程 → 页面走已有的 `webkit_web_view_evaluate_javascript`。

**实测修正（第一版 shim 无效，已修）**：第一版只对
`document.querySelectorAll('audio,video')` 设 `.volume`，用户 DP-1 上的
`3650874083` 静音**完全无效**——`parec` 捕获显示静音前后 peak 7301 → 7120
（等于没动）。两个叠加原因：

1. 该壁纸的 BGM 是 `new Audio()`（**游离元素**），不在 DOM 里，
   `querySelectorAll` 根本看不见它；
2. 页面自己会重写 `bgm.volume`（该壁纸 524/530/532 行，配合 ducking/暂停），
   一次性赋值随后就被覆盖。

修正后的做法是**不赋值而合成**：

- patch `HTMLMediaElement.prototype.volume` 的 setter：页面写多少记进
  `__xwVol`，真正落到元素上的永远是 `eff(v) = muted ? 0 : v * master`；
  getter 仍返回页面自己设的值，所以页面自己的淡入淡出、ducking 不受影响
  （否则页面写 `.volume = .volume - 0.01` 这类淡出会跟 master 反复相乘）；
- 元素登记进列表（DOM 扫描、`MutationObserver`、`play()` 钩子、setter 本身），
  `sync()` 遍历列表 —— 这是 master 唯一能到达 `new Audio()` 的途径；
- 音量语义：**web 上是缩放倍率**（100 = 页面原始音量，0 = 静音），
  video 上是绝对值。因为 web 页面自带混音，覆盖式接管会跟页面互相打架。

真机验证（`parec` 捕获独立 null sink）：
`3650874083`（元素式）静音后 peak=0，`volume 30` peak 7301 → 2000（≈0.27）；
`3768853831`（AudioContext）静音后 peak=0，`volume 30` peak 6315 → 1717。
修复前同样步骤静音后 peak 仍有 7120。

**实测修正（第二版）**：上表只验的是 `xwallpaper mute` 直控；**按键路径在 web 上从不生效**。
`web` 把 WebKit 的 GTK 子窗口 reparent 到壁纸窗口上，指针落点在最深的 WebKit 视图窗口，
而 WebKit 为页面输入在该窗口上保留 `GDK_KEY_PRESS_MASK`（GDK 经 XI2 选键，故 X 服务端的
core `all_event_masks` 看不出来），KeyPress 被它吃掉，永远冒泡不到守护进程自己的壁纸窗口
——`wallpaper.keys` 在 web 上等于摆设；video 的 mpv 子窗口不收键，所以 video 正常。
修法：`web_init` 里对 GTK toplevel 与视图窗口清掉 `GDK_KEY_PRESS_MASK|GDK_KEY_RELEASE_MASK`
（并在视图 `map-event` 时重清），让 X 把 KeyPress 冒泡到壁纸窗口，与 video 走同一条派发路径。
壁纸页不需要键盘输入，无副作用。回归：`test_web` 的
`nested web windows do not swallow wallpaper.keys` 断言两层子窗口都不含 `GDK_KEY_PRESS_MASK`
（撤掉修复即红）。真机 `parec`：给 web 壁纸挂上 keybinds 后按 `-`，peak 7418 → 0，再按恢复。

### 协议（`src/proto.c/.h`）

新增与 `set-prop` 同形的动词，保持向后兼容（旧 daemon 只报未知命令）：

```
control\0<target>\0<name>=<value>\n
  target: primary | all | <output-name>
  name:   volume | mute | pause | seek | rotate
```

按键派发**不走协议**（窗口内直接调用）；协议只服务 CLI 直控。

## Project Structure

```
src/keybinds.c/.h   按键文件解析与匹配（纯逻辑，单测主力）
src/x11.c           窗口 event_mask 增 KeyPressMask
src/app.c/.h        KeyPress 派发；xw_app_control（CLI 直控）
src/daemon.c        control 动词路由
src/proto.c/.h      control 编解码
src/cli.c/.h        volume / mute 子命令 + keybinds 默认路径
src/backend.h       control 回调
src/video.c         mpv 实现（volume/mute/pause/seek/rotate）
src/web.c           JS shim + 注入 + evaluate + control
src/scene.c         SDL2_mixer 音量/静音；pause → set_paused
tests/test_keybinds.c       新增：解析/匹配/边界
tests/test_cli.c, tests/test_client.c   子命令与协议
tests/integration/          端到端
docs/specs/wallpaper-control.md  本文件
```

## Code Style

沿用仓库风格（C11、`xw_<module>_<action>`、无冗余注释）：

```c
int xw_keybinds_parse(const char *text, XWBind *out, int max, XWError *err) {
  int n = 0;
  for (const char *line = text; line && *line && n < max; ) {
    const char *eol = strchr(line, '\n');
    size_t len = eol ? (size_t)(eol - line) : strlen(line);
    if (xw_keybinds_line(line, len, &out[n]) == 0)
      n++;
    line = eol ? eol + 1 : NULL;
  }
  return n;
}
```

## Testing Strategy

- **单元**（`make test`）：
  - `test_keybinds`：用户真实的 `wallpaper.keys` 全文 → 9 条绑定、逐条断言 kind 与参数；
    注释/空行/未知动作/超长行/无参数动作的边界；键名归一化。
  - 动作语义纯函数：`xw_audio_apply(cur, "+2")` 增减与钳位（0..100）；
    `xw_rotate_cycle(values, n, cur)` 轮转。
  - `test_cli` / `test_client`：子命令解析、`control` 编解码。
- **集成**（`make test-xvfb`）：
  - Xvfb 里给一个窗口装按键文件，合成 KeyPress（`XSendEvent`/`XTestFakeKeyEvent`），
    断言对应后端收到动作（video 用 mpv 属性回读；scene 用可查询的音量）；
  - web：init 后注入 shim 并 evaluate，断言无错误（真实音量变化人工听测）。
- **手动冒烟**：真实会话聚焦壁纸窗口按 `9/0/-/=/h/j/k/l/r`，逐条确认。

## Boundaries

- **Always**：跑 `make test` / `make test-xvfb` / `make check-client-libs`；
  协议向后兼容；不支持的动作对按键静默、对 CLI 报错。
- **Ask first**：改动 `--keybinds` 的公开语义（本次已在 spec 里明确从
  "mpv 专用"变为"守护进程解析"）、给 web 注入音频控制以外的脚本、
  引入新的按键抓取方式（全局 XGrabKey）。
- **Never**：把音量写进 state 文件（本次不做持久化）、
  对 root 做 XGrabKey（会抢走所有程序的按键）、引入新依赖。

## Success Criteria

1. 聚焦一个壁纸窗口按 `wallpaper.keys` 里的键，**video / web / scene** 上
   volume / mute / pause 都生效；seek 在 video 与 web 上生效；
   rotate 在 video 上生效；不支持的动作不报错、不影响其它窗口。
2. 多屏时只有**被聚焦的那个窗口**响应；其它屏的壁纸不受影响。
3. mpv 不再接收 `input-conf`，同一个键不会被处理两次（按 `0` 音量只 +2 一次）。
4. `xwallpaper volume/mute` 直控与 `-m/--all` 行为正确；image 上明确报错。
5. web：元素式与 Web Audio 两种机制下音量都变；页面重载后保持。
6. 三个门禁全绿；README 增补按键文件与动作说明。

## 已确认的决策（原 Open Questions）

- volume 同时支持相对（`+2`/`-2`）与绝对（`50`）。
- 不做持久化（`restore` 回到 state 里的值）。
- 按键由**守护进程接管**，mpv 不再接 `input-conf`。
- 动作范围：**完整覆盖** `wallpaper.keys`（seek/volume/mute/pause/rotate）。
- 目标选择：**聚焦窗口**（不做全局抓键）。

## 实现状态（全部完成）

| 任务 | 提交 | 验证要点 |
| --- | --- | --- |
| keybinds 解析/匹配 | `9943928` | 用户真实文件逐条解析，10/10 单测 |
| 动作算术 | 同上 | 音量相对/绝对 + 钳位、旋转轮转 |
| control 通道 + video + CLI/协议 | `f137fa0` | daemon 下读回 mpv 属性：50→60、绝对 30、mute 往返 |
| 按键派发到聚焦窗口 | `80a8509` | `xdotool` 真机按键：`0 0 0` = +6（无双重处理）、`-/=/r` 生效 |
| web | `ca3f217` | fixture 同时含 `<audio>` 与 `AudioContext`，两条路径都断言；重载后保持 |
| web 静音失效修复 | 本次 | 游离 `new Audio()` 不可见 + 页面重写 `.volume`；真机 `parec`：静音 peak→0，修复前仍 7120 |
| web 按键失效修复 | 本次 | WebKit 子窗口吃掉 KeyPress；清嵌套窗口键掩码；`test_web` 断言 + 真机 `parec` |
| scene | `ecd0d81` | 真机：`=` 暂停时逐帧相同、恢复后逐帧不同 |
| 多目标语义 + 文档 | 本次 | `--all` / `-m` / 未知输出报错；README 增补按键与动作章节 |

### 实现与本文档的两处偏差（有意为之，记录在此）

1. **旋转的 ring 未按文件取值**：按键触发 `rotate` 时统一用默认环
   `90 180 270 0`（与仓库内所有已知 key 文件一致）。若将来出现自定义环，
   需要把环一路传到后端（`control` 的 value 已在协议中预留了位置）。
2. **`pause` 只走按键路径**，没有 `xwallpaper pause` 子命令——用户确认的 CLI
   范围只有 volume/mute；要加的话只是 `cli.c` 里再加一个分支。

### 已知不覆盖

- Wayland 下按键派发未做（Wayland 无法在应用侧抓全局键，且 dwl 无对应协议）；
  Wayland 用户走上面的 CLI 绑键路径即可，`control` 本身与平台无关。
- 音量/静音**不写回 state 文件**（本次范围），`restore` 会回到设置时的值。
