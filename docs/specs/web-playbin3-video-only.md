# Spec: web-playbin3-video-only（无音轨 webm 卡在加载中）

## 现象

Wallpaper Engine 壁纸 `3828392144`（`/home/walter/Desktop/Wallpapers/web/wallpaper-engine/3828392144`）里有 84 个只在 `media/1080p/` 下、**没有音轨**的
webm（`ffprobe` 只报 `vp9`）。播放到这些穿搭时，页面一直停在“正在为你准备…”
加载提示上：`<video>` 有正确的 `src`、`duration=12.067`、`paused=false`，但
`currentTime` 永远停在 0.01s 左右，`readyState` 最高 2（`HAVE_CURRENT_DATA`），
`networkState` 保持 2（`LOADING`）。

`loop = true` 或换用外部 `python3 -m http.server` 有时能正常播放，说明不是
编码/WebKit 本身不能解码，而是本地 HTTP + WebKit 媒体管线的时序问题。

## Root cause（实测 GStreamer/WebKit 日志）

`GST_DEBUG=webkitmediaplayer:5` 的失败日志：

```
[Buffering] mode: GST_BUFFERING_DOWNLOAD, status: 100.000000%   ← 整文件已下载
Stream is fully downloaded, stopping monitoring downloading progress.
...
[Buffering] mode: GST_BUFFERING_DOWNLOAD, status: 3.000000%     ← 又来一条旧的 buffering
[Buffering] Updated maxTimeLoaded: 0.362010
[Buffering] Pausing stream for buffering.
```

- 非 live 媒体默认走 legacy `playbin` + **on-disk buffering**
  （`downloadbuffer`）。本地回环下整个 6MB 文件在播放开始前就下载完
  （`GstCacheDownloadComplete`）。
- 无音轨的 webm 没有第二条流/音频时钟拉动，`downloadbuffer` 在下载完成后仍
  会补发一条 3%（≈首 GOP 的 0.362s）的 `GST_MESSAGE_BUFFERING`。
  WebKit 的 `updateBufferingStatus()` 已经先收到 100%，随后被这条 3% 覆盖，
  进入 `[Buffering] Pausing stream for buffering`；此时采集定时器已被
  `GstCacheDownloadComplete` 停掉，不会再有 100% 把它唤醒，于是永久缓冲。
- 有音轨的文件同一条路径能过（音频时钟/第二条流让 buffering 状态收敛），
  所以只有这些 `1080p` 无声 webm 触发。
- WebKit 源码在 `updateDownloadBufferingFlag()` 里对 `player->isLooping()`
  直接 `disableDownloading()`，注释就是 “deadlock-prone on-disk buffering”；
  这解释了为什么 `loop=true` 偶尔绕过问题。

## Fix

在 WebProcess 启动前把 `WEBKIT_GST_USE_PLAYBIN3=1` 注入环境
（`src/web.c` 的 `web_pin_gst_env()`，与已有的 `openalsink:NONE` 同一个开关，
在 `web_attach_context()` 最前面调用）：

- `playbin3`（`uridecodebin3`/`urisourcebin`）不会落进同一条
  `downloadbuffer` 停滞；实测同一文件稳定从 0 播到 12s。
- WebKit 本身对 MSE / `blob:` / `mediastream:` **已经强制用 playbin3**
  （`MediaPlayerPrivateGStreamer::createGSTPlayBin()`），所以这个变量只改
  “普通 media” 路径，MSE 壁纸不受影响。
- 若用户显式设置了该变量（含 `0`），保留用户值，不覆盖。

## 与上次修复的关系（27c9827）

`27c9827 fix(web): keep HTTP connections alive for pipelined Range seeks`
（2026-10-30）解决的是**另一个**上游问题：内嵌 HTTP 每个连接只服务一次，
WebKit 为长视频无音轨 webm 管道化发来的 Range 请求被丢掉，同样表现为
`readyState=2` + 一直“准备中”。

这次不是那个回归：

- 实测失败请求只有一条 `GET …webm`，**没有 Range**，`Connection: close`，
  服务器把 6,153,999 字节完整发完（`body done remaining=0`），不存在丢请求。
- 用 `27c9827` 完整代码在当前系统重建，同一个无声 webm 仍然卡在 `t≈0.02s`，
  说明上次的修复并没有覆盖现在这条路径。
- 上次修复后 `web.c` 的媒体启动路径没有改动；环境里 GStreamer 从 1.28.8
  升到 1.28.9（2026-11-10，WebKit 仍为 2.54.2），但 WebKit sandbox 不继承
  `LD_LIBRARY_PATH`，无法直接用 1.28.8 库做 A/B，故不把版本当作已证实结论。
- 本壁纸目录创建于 2026-11-15 00:27（新下载内容），也不排除旧壁纸从未命中
  这个时序。

结论：症状相同、根因不同；本次用 playbin3 绕开 downloadbuffer 死锁。

## Verification

- 真壁纸：`xvfb-run … probe` 选中 index 43（`sit-extra-19`，无音轨）连续 3 次
  `t=1.94/3.97s`（修复前恒为 `0.007s`）；index 3（4K + opus）仍正常。
- 合成 fixture：1080p/12s/4Mbps 无音轨 webm，修复前 3/3 卡在 0.02s，
  修复后 2/2 正常推进。
- `make test` / `make test-xvfb`：新增 `test_web_gst_playbin3_pinned`（无修复时
  环境变量为空，红灯）；其余 web 用例全绿。

## Boundaries

- 只影响普通 `<video>/<audio>` 播放；mpv 的 video backend 与 scene 后端无关。
- 不处理“有音轨但极短/损坏”的媒体，也不改变 HTTP 层（`Accept-Ranges`、
  keep-alive 逻辑保持原样）。
- 这是对 WebKitGTK 2.54.2 + GStreamer 1.28.9 的 workaround；升级后可复查是否
  仍需要（`WEBKIT_GST_USE_PLAYBIN3` 变成默认或 bug 修复即可删除）。
