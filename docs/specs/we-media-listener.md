# Spec: we-media-listener (Wallpaper Engine 媒体联动)

## Objective

给 `web` 壁纸补上 Wallpaper Engine 的媒体联动 API，让依赖
`wallpaperRegisterMediaProperties/Thumbnail/Playback/TimelineListener` 的壁纸
（首个目标：Sonic Topography, workshop 3747222633）在 `xwallpaper` 下能显示
歌曲标题、艺术家、专辑封面、播放状态和进度。

数据源是 Linux 桌面标准的 **MPRIS over D-Bus**，不是音频频谱，也不是
Wallpaper Engine 的 Windows GSMTC。只读，不控制播放器。

User stories:
- 我在 Spotify/Chromium/VLC 里放歌，web 壁纸的播放器面板出现并显示歌名、歌手、封面。
- 我暂停/继续/切歌，页面收到对应事件；暂停后面板按壁纸自身逻辑隐藏。
- 我同时开多个 web 壁纸，它们共享一份 MPRIS 数据；没有媒体监听者的壁纸不连 D-Bus。
- 我不装任何媒体播放器时，壁纸行为与现在一致：不报错、不黑屏、无额外线程。

本 spec 关闭 `docs/specs/we-audio-listener.md` 中列出的
`wallpaperRegisterMediaProperties/Thumbnail/Playback/Timeline` 非目标；实现时同步
更新那一行。

## Assumptions

1. 数据源只做 MPRIS（`org.mpris.MediaPlayer2.*`）；不做 Windows、macOS、Pulse
   应用级抓取。Linux 上 Spotify、Chromium、VLC、mpv 等都会发布 MPRIS。
   MPD 本身不实现 MPRIS，首版通过 `mpdris2-rs`（`org.mpris.MediaPlayer2.mpd`）
   接入，不实现 MPD 原生协议。
2. 只读；不实现播放/暂停/seek 控制。页面自己的 `<audio>/<video>` 控制仍由现有的
   `__xwCtlMedia` 负责，两者互不替代。
3. `gio-2.0` 已经通过 `gtk+-3.0`/`webkit2gtk-4.1` 进入 daemon 链接，不算新增依赖。
   不新增 libsoup、gdk-pixbuf、json-c 的硬依赖。
4. 媒体联动只在页面注册监听器时启动；没有监听者时 `xwallpaperd` 不连 session bus。
5. 封面：`file://` 读成 `data:image/...;base64,...`；`http(s)://` 直接作为 URL
   交给页面加载；`data:` 原样透传。首批不做远程下载缓存。
6. `primaryColor` / `textColor` 首版发送空字符串。目标壁纸不使用这两个字段；
   为避免引入取色解码依赖，后续再补。
7. 播放状态常量与 WE 一致：`PLAYING=0`, `PAUSED=1`, `STOPPED=2`。

→ 以上若有误现在纠正，否则按此 spec 进入 PLAN。

## Tech Stack

- C11，daemon 侧新增 `src/we_media.[ch]`，使用已有的 GIO/GDBus：
  `g_bus_get_sync(G_BUS_TYPE_SESSION, ...)`、`g_dbus_connection_call*`、
  `g_dbus_connection_signal_subscribe`。
- 不改 `xwallpaper` CLI，不新增 CLI 参数。
- 新增 `gio-2.0` 到显式链接（若现有 webkit pkg-config 已带 `-lgio-2.0` 则只加注释，
  不改依赖面）。
- JS shim 与音频 shim 一样在 `WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START` 注入。

## Commands

```
Build:      make
Unit:       make test
Integ:      make test-xvfb
CLI guard:  make check-client-libs
Debug:      make DEBUG=1
Manual:
  xwallpaperd &
  xwallpaper set ~/.../3747222633
  # 另开终端播放 MPRIS 媒体（Spotify/Chromium/VLC）
  gdbus call --session --dest org.freedesktop.DBus \
    --object-path /org/freedesktop/DBus \
    --method org.freedesktop.DBus.ListNames | tr ',' '\n' | grep mpris
```

## Project Structure

```
src/we_media.h/.c        MPRIS 连接、玩家发现、状态快照、JS 载荷格式化（纯逻辑与 bus 分层）
src/web.c                媒体 shim 注入、每个 view 的 want/seen 位、JS push、xw_web_media_tick
src/web.h                暴露 xw_web_media_want/tick/push/eval_count
src/app.c                X11/Wayland 主循环调用 xw_web_media_tick()
Makefile                 DAEMON_SRC、HEADER_DEPS、单测/集成测试规则
tests/test_we_media.c    纯函数单测（格式化、Metadata 解析、玩家选择、时间转换）
tests/integration/test_web_media.c
                          Xvfb：页面注册 + 合成快照 push，验证 JS 回执
tests/data/wemedia/      媒体联动 fixture 页面（与 webmedia 的页面内媒体测试分开）
docs/specs/we-audio-listener.md
                         把媒体联动从“非目标”改为指向本 spec
README.md / CAPABILITY.md
                         实现后补一行能力说明（可选）
```

## Data Model

```c
/* we_media.h：不包含 GTK/WebKit/X11 头，便于单测 */
typedef struct {
  char title[512];
  char artist[512];
  char album[512];
  char content_type[32];  /* "music" 默认 */
  char *thumbnail;        /* data:... 或 http(s) URL；NULL 表示无 */
  int state;              /* 0 playing, 1 paused, 2 stopped */
  double position;        /* 秒 */
  double duration;        /* 秒，0 表示 unknown */
  int enabled;            /* media integration 是否可用 */

  /* 单调递增的字段版本，web.c 用它做每 view 去重 */
  uint64_t v_props, v_thumb, v_playback, v_timeline, v_status;
} XWMediaInfo;
```

内部玩家表：

```c
typedef struct {
  char name[256];      /* org.mpris.MediaPlayer2.xxx */
  char owner[64];      /* unique bus name */
  int state;
  double base_position;
  int64_t base_mono_us;
  XWMediaInfo info;    /* 该玩家的元数据/封面/时长 */
} XWMediaPlayer;
```

## Behavior

### MPRIS 发现与选择

1. 启动时 `ListNames`，筛 `org.mpris.MediaPlayer2.` 前缀；跳过
   `org.mpris.MediaPlayer2.playerctld`（代理会把同一首歌重复暴露）。
2. 订阅 `org.freedesktop.DBus` 的 `NameOwnerChanged`，处理播放器出现/退出。
3. 对每个播放器 `GetAll("org.mpris.MediaPlayer2.Player")` 拿
   `PlaybackStatus`、`Metadata`、`Position`、`Rate`。
4. 订阅每个播放器 `/org/mpris/MediaPlayer2` 的 `PropertiesChanged`，以及
   `Seeked`。
5. 活动播放器：`Playing` 优先；多个 `Playing` 取最近进入 Playing 的；
   没有 Playing 取最近活跃的 `Paused`；都没有则清空快照（`enabled=1`，
   `state=STOPPED`，标题为空）。
6. 玩家退出或 `Stopped` 后，如果还有别的 Playing/Paused，自动切过去。

已知来源：MPD 通过 `mpdris2-rs` 暴露为 `org.mpris.MediaPlayer2.mpd`；
`mpdris2-rs` 用 MPD 的 `readpicture/albumart` 生成 `mpris:artUrl`，
所以本地/远程 MPD 都能带封面。xwallpaper 不直接连 MPD 的 6600 端口。

### MPRIS → WE 字段

| MPRIS | WE |
|---|---|
| `PlaybackStatus` | `state` 0/1/2 |
| `Metadata["xesam:title"]` | `title` |
| `Metadata["xesam:artist"]`（`as`，也兼容 `s`） | `artist`，多值用 `", "` |
| `Metadata["xesam:album"]` | `albumTitle` |
| `Metadata["mpris:artUrl"]` | `thumbnail`（见下） |
| `Metadata["mpris:length"]`（int64 或 uint64，微秒） | `duration`（秒） |
| `Position`（微秒） | `position`（秒） |
| `Metadata["xesam:url"]` / 扩展名 | `contentType`，默认 `music` |

### 时间线

- 以 `base_position + (now - base_mono)/1e6` 外推；暂停时冻结。
- `Seeked`、播放/暂停切换、切歌时重置基准并立即推一次。
- 播放中每秒推一次 `timeline`；`duration=0` 时页面自己不显示进度条。
- 目标壁纸的 properties 回调会把 `position/duration` 归零，所以 **props 版本变化时
  强制同时推一次 timeline**。

### 封面

- `artUrl` 为空：推空字符串，页面隐藏封面。
- `file://` 或绝对路径：读文件，按魔数判断 PNG/JPEG/WebP/GIF，生成
  `data:image/<mime>;base64,<...>`；上限 2 MiB 原始数据，超限推空并记一次 warning。
- `http(s)://`：原样作为 `thumbnail` 推给页面（`<img src>` 直接加载），不阻塞主循环。
- `data:`：原样透传。
- `primaryColor`/`textColor`：空字符串（见 Assumptions）。

### JS shim

文档开始时注入：

```js
window.wallpaperMediaIntegration = {
  PLAYBACK_PLAYING: 0, PLAYBACK_PAUSED: 1, PLAYBACK_STOPPED: 2,
  playback: { PLAYING: 0, PAUSED: 1, STOPPED: 2 }
};
window.wallpaperRegisterMediaPropertiesListener = ...
window.wallpaperRegisterMediaThumbnailListener = ...
window.wallpaperRegisterMediaPlaybackListener = ...
window.wallpaperRegisterMediaTimelineListener = ...
window.wallpaperRegisterMediaStatusListener = ...
window.__xwMediaProps = function (o) { ... };
window.__xwMediaThumb = function (o) { ... };
window.__xwMediaPlayback = function (o) { ... };
window.__xwMediaTimeline = function (o) { ... };
window.__xwMediaStatus = function (o) { ... };
```

- 每个 register 函数把回调 push 进对应数组，并通过
  `webkit.messageHandlers.xwMedia.postMessage(kind)` 通知 native。
- native 收到 kind 后设置该 view 的 want 位，并把 `seen_*` 归零，下一次 tick
  立即补发当前快照（解决“页面注册晚于数据到达”）。
- 同一 kind 多次注册：全部回调都收到（与现有音频 shim 一致）。
- `wallpaperRegisterMediaStatusListener` 收 `{enabled:true/false}`；session bus
  连不上时发 false。

### 与 web 生命周期

- `WebCtx` 新增 `media_want` 位图、`seen_props/thumb/playback/timeline/status`、
  `last_media` 不需要（版本号即可去重）。
- `xw_web_media_tick()`：没有 want 时 `xw_we_media_stop()`；有 want 时确保
  `xw_we_media_start()`，更新外推时间，取快照并 `xw_web_media_push()`。
- `xw_web_media_push(const XWMediaInfo *)` 是公开函数，集成测试可直接喂合成快照。
- 每个字段只在 `info.v_* > view.seen_*` 时 eval 一次；eval 成功才推进 seen。
- 播放器切换/清空时 bump 所有相关版本，保证页面收到“空”状态。
- `app.c` 的 X11/Wayland 两个主循环都调用 `xw_web_media_tick()`；
  `app_poll_timeout_ms()` 在 media want 时也降到 50ms，降低面板出现延迟。

## Code Style

```c
/* 纯逻辑：可单测，不碰 D-Bus */
int xw_we_media_format_props(const XWMediaInfo *m, char *out, size_t n);
int xw_we_media_format_playback(const XWMediaInfo *m, char *out, size_t n);
int xw_we_media_parse_metadata(GVariant *metadata, XWMediaInfo *m);

/* bus 层：只在主线程调用 */
int xw_we_media_start(void);
void xw_we_media_stop(void);
const XWMediaInfo *xw_we_media_get(void);
```

- 命名 `xw_we_media_*`；结构体 `XWMedia*`。
- 所有 D-Bus 回调都在 GTK 默认 main context；不加线程、不加锁。
- JSON 字符串必须转义 `"`、`\` 和 `<0x20` 控制字符；UTF-8 原样透传。
- 不在主循环里做同步网络请求；封面远程 URL 不下载。

## Testing Strategy

### 单测（`make test`，无 X/D-Bus）

`tests/test_we_media.c`，链接 `src/we_media.o` + `gio-2.0`：

1. `format_props/playback/timeline/status/thumb` 的 JSON 合法性：
   引号、反斜杠、换行、中文、空字段；用 `json-c`（可用时）解析回来断言，
   否则做精确字符串断言。
2. `parse_metadata`：构造 `a{sv}`，覆盖 artist 为 `as`、artist 为 `s`、
   缺字段、`mpris:length` 为 int64、artUrl 为 file/http。
3. 玩家选择：Playing > Paused > Stopped；多个 Playing 取最近活跃；
   `playerctld` 被跳过。
4. 时间换算：微秒→秒、暂停冻结、播放外推、duration 缺失时为 0。
5. 封面分类：`file://`/绝对路径 → 读；`http(s)` → 透传；`data:` → 透传；
   超限 → 空。

### 集成（`make test-xvfb`）

`tests/integration/test_web_media.c`：

- fixture `tests/data/wemedia/index.html` 注册全部监听器，把收到的事件写进
  `document.title`。
- 启动 web backend，pump GTK，断言 `xw_web_media_want()` 从 0 变 1。
- 调 `xw_web_media_push()` 喂合成快照（标题/歌手/封面 data URL/playing/12.5/180），
  断言页面收到所有字段。
- 同一版本再 push 一次，`xw_web_media_eval_count()` 不增长（去重）。
- 版本递增/切换成空快照，页面收到清空。
- destroy 后 `xw_web_media_want()` 回 0。

### 手动

- Spotify/Chromium 播放 → 目标壁纸右上角面板出现；切歌封面/标题淡入淡出；
  暂停 3 秒后面板按壁纸逻辑隐藏；继续播放后重新出现。
- MPD + `mpdris2-rs`：启动桥后 `org.mpris.MediaPlayer2.mpd` 出现，
  目标壁纸显示 MPD 当前曲目和封面。
- 两个 web 壁纸同时存在，都收到同一首歌。
- `xwallpaper clear` 后 `gdbus` 侧无残留订阅（可用 `busctl monitor` 观察）。
- 无播放器时壁纸正常，不报错。

## Boundaries

- Always: 只读 MPRIS；失败的 D-Bus 调用降级为空数据；`make test`、`make test-xvfb`、
  `make check-client-libs` 全过再合；所有 bus 回调在主线程；不阻塞 UI。
- Ask first: 新增 libsoup/gdk-pixbuf/json-c 硬依赖；改 protocol；支持远程封面下载缓存；
  实现播放控制。
- Never: 把 GIO/GDBus/GTK 链进 `xwallpaper` CLI；采集麦克风；上传/外发媒体元数据；
  为过测试删断言；在页面里注入未转义的用户字符串。

## Success Criteria

1. `xwallpaper set ~/.../3747222633` 后播放任意 MPRIS 媒体，右上角面板在 1 秒内出现，
   显示标题、艺术家，封面/进度条在数据可用时显示。
2. `wallpaperRegisterMediaProperties/Thumbnail/Playback/Timeline` 四个回调都能收到
   与 MPRIS 对应的数据；暂停/继续/切歌/seek 状态正确。
3. 无媒体监听者的壁纸不建立 session bus 连接；无播放器时无错误输出。
4. 多 web 壁纸共享一份 MPRIS 连接；`clear` 后连接和订阅释放。
5. 单测覆盖所有格式化/解析纯函数；集成测试覆盖注册、推送、去重、清空。
6. MPD 通过 `mpdris2-rs` 出现时，行为与其它 MPRIS 播放器一致（含封面）。
7. `make test`、`make test-xvfb`、`make check-client-libs` 全绿。

## Open Questions

首版已按以下默认决策实现，没有阻塞项：

1. 远程封面（Spotify 常见 `https://i.scdn.co/...`）直接透传 URL；本地/`file://`
   封面转 Base64 data URL。需要“所有封面都 Base64”时再排期下载缓存。
2. `primaryColor`/`textColor` 首版为空字符串；目标壁纸不使用。取色另开 task。
3. 活动播放器：Playing > 最近活跃 Paused，跳过 `playerctld`。
4. `wallpaperRegisterMediaStatusListener` 首版发送 `{enabled}`。
