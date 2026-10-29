# Spec: we-audio-listener (Wallpaper Engine 音频监听)

## Objective

给 `web` 壁纸补上 Wallpaper Engine 的 `window.wallpaperRegisterAudioListener(cb)`，
解决 `Sonic Topography (3747222633)` 这类音频可视化壁纸在 `xwallpaper` 下“没效果”
（只有空闲呼吸微动，不随音乐跳）的问题。

User stories:
- 我 `xwallpaper set ~/.local/share/Steam/.../3747222633` 后，播放音乐时 160×160 频谱柱、
  涟漪、流星随节拍触发；静音时退回壁纸自带的空闲波浪。
- 我切歌/换默认输出设备后，无需重设壁纸，音频跟随新的默认输出。
- 我没有音频壁纸时，行为零变化：daemon 不连 Pulse，无额外 CPU；`make check-client-libs` 仍过。

非目标：`wallpaperRegisterMediaProperties/Thumbnail/Playback/Timeline` 媒体联动全套；
`wallpaperPropertyListener` 已有投递不动；`wallpaperReady` 不补（无害空调用）；
麦克风采集；逐应用采集；Wayland `web_init_wl` 首版可跟随（若成本大则 X11 先行，Wayland 对齐另起 task）。

## Tech Stack

- C11，daemon 侧新增 `libpulse`（`pkg-config --exists libpulse`）。理由：实测本机
  `Server: PulseAudio (on PipeWire 1.6.9)`，`libpulse` 经 `pipewire-pulse` 直接可用，
  无需新守护进程；原生 PipeWire API 代码量更大，留作后续。
- FFT 自带小实现（`src/we_audio.c` 内 ~100 行 Hann + radix-2 / 或 kissfft 单文件，
  待 plan 阶段定），**不新增 `fftw` 依赖**。
- 关键 API：`pa_simple` 或 `pa_stream` 连默认 sink 的 `.monitor` 源
  （`@DEFAULT_MONITOR@` 优先，失败则枚举 `default_sink.monitor`），
  `float32le 2ch 44100Hz` 采集；1024 帧做一次 FFT → 128 floats（0–63 左，64–127 右，
  band b 取 FFT bin 2b（与 linux-wallpaperengine 的 PulseAudioPlaybackRecorder 同式），输出钳位 0..1（上游静音附近 band 为负值，此处下钳为 0 以保 JS 监听器的 0..1 契约，着色器渲染与上游一致）。
- 桥接：`web.c` 以 `document-start` user-script 注入 shim 定义
  `window.wallpaperRegisterAudioListener`（存回调数组，支持重复注册覆盖语义与 WE 一致：
  后注册者也收到数据）；daemon 音频线程每 50ms 对**有监听器的 view** 逐个
  `webkit_web_view_evaluate_javascript(__xwAudioPush([...]))`。单次 payload ~1KB，
  20Hz 下可接受。

WE 语义锚点（已核实）：
- `audioArray` 定长 128，`0–63` 左声道、`64–127` 右声道，各自低频在前、高频在后。
- 回调频率约 20–30Hz；页面要求**不要**在 `window.onload` 里注册，shim 必须在
  `document-start` 就绪（`web_apply_wallpaper_policy` 同一位置）。
- 无声时 WE 照常推送全 0 数组；本方案同样推送全 0（而不是停推），壁纸据此做
  idle/active 切换。

## Commands

```
Build:      make
Test:       make test
Integ(X11): make test-xvfb
Dep guard:  make check-client-libs
Manual smoke:
  ./xwallpaperd &
  ./xwallpaper set ~/.local/share/Steam/steamapps/workshop/content/431960/3747222633
  paplay music.wav &
  ./xwallpaper --list
  ./xwallpaper clear
  ./xwallpaper exit
```

## Project Structure

```
src/we_audio.[ch] (new) → 纯逻辑 + 采集线程：monitor 连接、ring buffer、FFT→128 bins、
                          归一化/JSON 构造；无 GTK/WebKit/X11 依赖，可单测。
src/web.c               → 改：document-start 注入 audio shim；view 注册/注销监听器；
                          订阅 we_audio 扇出（多窗口共享一份采集）。
src/app.c               → 改（小）：web view 存在性变化时启停采集（refcount，无监听则断开 Pulse）。
Makefile                → 改：libpulse 只进 daemon 链接（CLI_OBJ 不变）；check-client-libs 加 pulse 断言。
tests/test_we_audio.c (new) → 单测：bin 映射、归一化、JSON 构造（无 Pulse/X）。
tests/integration/test_web_audio.c (new) → Xvfb：合成正弦喂 we_audio 映射函数，
                          断言页面 JS 收到 128 数组且低频 bin 显著高于高频。
docs/specs/we-audio-listener.md → 本文件
```

JS shim 形状（示意，最终以 plan 为准）：
```js
window.__xwAudioCBs = [];
window.wallpaperRegisterAudioListener = function(cb){
  if (typeof cb === 'function') window.__xwAudioCBs.push(cb);
};
window.__xwAudioPush = function(arr){
  window.__xwAudioCBs.forEach(function(cb){ try{cb(arr);}catch(e){} });
};
```

## Code Style

```c
/* we_audio.h — 无 Pulse/GTK/X11 头，可单测 */
#define XW_WE_AUDIO_BINS 128
int xw_we_audio_map(const float *l, const float *r, int n, float out[128]);
/* l/r: 时域采样（已加窗/归一前）; 返回 0=ok. 命名 xw_we_audio_*，错误走 XWError。 */
```

- 命名沿用 `xw_<module>_<action>`；线程用 POSIX thread + 条件变量，`stop` 时 join；
  `destroy` 释放 Pulse 句柄与 ring buffer。
- 进程内单例：一份采集，多 web view 扇出；最后一个监听者注销 → 断开 monitor，
  回到零开销。

## Testing Strategy

- 单测（无 X/Pulse，`make test`）：正弦扫频 → 低频 bin 峰值位置正确；静音 → 全 0；
  满幅 → 钳制 ≤1.0（或按 WE 语义允许微超但单测断言 ≤1.5）；JSON 构造长度恒 128。
  纯逻辑目标 ~90%。
- 集成（Xvfb，`make test-xvfb`）：`test_web_audio` 用合成数据调
  `__xwAudioPush`，泵 GTK 后读页面回执（`document.title` 或隐藏 div），断言收到
  128 数组；再测注销后不再推送。
- 手动：Sonic Topography + `paplay` 放歌，观察频谱/涟漪/流星随节拍；`pavucontrol`
  切默认输出后 5s 内跟随；`--list`/`clear`/`restore` 不挂。
- 回归门：`make check-client-libs` 必须过（`libpulse` 只进 `xwallpaperd`）；
  无音频壁纸时 `pactl list clients` 无新增 xwallpaper 音频 client。

## Boundaries

- Always: `make test` 全绿再合；`web.c` 不开第二个 X 连接；`destroy` 释放 view/shim/
  音频订阅；无声/无 monitor 时推送全 0 而非报错黑屏；只读 monitor，绝不写用户壁纸文件。
- Ask first: 新增构建依赖（本方案新增 `libpulse`，已在此 spec 确认；再加别的需另议）；
  改 protocol 文本格式（本方案不需要）；动 `web_init_wl`（首版可 X11-only，Wayland 对齐另起 task）。
- Never: 采集麦克风；把 `libpulse`/webkit/mpv/imlib2 链进 `xwallpaper` CLI；
  `fork+exec` 外部录音进程；提交 secret；为过测试删断言。

## Success Criteria

1. `set <3747222633目录>` 上屏后，页面 `wallpaperRegisterAudioListener` 回调被调用
   （`wallpaperAudioReceived=true`，不再是纯 idle 模式），放歌时频谱柱跳动。
2. 推送数组恒 128 floats，`0–63/64–127` 左右声道，低频→高频；20Hz±5Hz；静音时全 0 且不断推。
3. 双 web 壁纸共存时两屏都收到数据；`clear` 后采集线程退出（无残留 Pulse client）。
4. 无监听器的纯静态 web 壁纸：不连 Pulse，CPU/行为与之前一致。
5. `make test`、`make test-xvfb`（有 xvfb 时）、`make check-client-libs` 全过。

## Open Questions

- 默认输出切换跟随：`pa` subscribe 自动跟 vs 5s 轮询，plan 阶段定（倾向 subscribe）。
- 增益/底噪门限：是否加可调灵敏度（首版固定 WE 风格归一化，不加 flag）。
- `web_init_wl` 是否同版本跟随：待 plan 评估成本，默认 X11 先行。
```

ASSUMPTIONS I'M MAKING:
1. 只做音频监听，不做媒体联动（已确认）。
2. 音源=系统默认输出回环，libpulse 经 pipewire-pulse（已确认）。
3. FFT 自带实现，不引入 fftw（保持依赖面最小）。
→ 以上若有误现在纠正，否则按此 spec 进入 PLAN。
