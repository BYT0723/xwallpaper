# Spec: we-web-events（general properties / setPaused / 音频 30Hz）

## Objective

补齐 WE web 壁纸两个常用回调，并把音频推送对齐到 WE 频率：

1. `window.wallpaperPropertyListener.applyGeneralProperties({fps})`：页面据此限制
   `requestAnimationFrame` 频率。xwallpaper 已能解析、持久化并传递 `--fps`，但 web
   后端忽略它。
2. `window.wallpaperPropertyListener.setPaused(bool)`：用户 `xwallpaper pause` 或
   DPMS 熄屏时通知页面冻结动画；当前只暂停页面媒体，JS 侧无感知。
3. 音频 `__xwAudioPush` 周期 50ms（20Hz）→ 33ms（≈30Hz），与 WE 文档一致。

User stories:
- 我 `xwallpaper set --web <dir> --fps 30` 后，页面 `applyGeneralProperties` 收到
  `{fps:30}`，其 rAF 循环按 30 帧限速。
- 我不给 `--fps` 时，页面不会收到 `applyGeneralProperties`（与 WE 未设置时一致）。
- 我 `xwallpaper pause` 或屏幕 DPMS 关闭时，页面 `setPaused(true)`；恢复时 `false`。
- 音频可视化壁纸的频谱刷新从约 20Hz 提升到约 30Hz，静音时仍推全 0。

非目标：真正拦截/包装 `requestAnimationFrame` 强制限帧（WE 也不做，页面自行遵守）；
`quiet`（锁屏静音不暂停）不触发 `setPaused`；麦克风/逐应用采集。

## Tech Stack

C11 + webkit2gtk-4.1；JS 通过 document-start shim 与
`webkit_web_view_evaluate_javascript` 投递；无新依赖。

## Commands

```
Build:      make
Test:       make test
Integ(X11): make test-xvfb
Dep guard:  make check-client-libs
Manual:     ./xwallpaperd & ./xwallpaper set --web tests/data/weevents --fps 24
```

## Project Structure

```
src/web.c                       → 改：WebCtx.fps；listener 状态投递；调用点；33ms
src/cli.c                       → 改：--fps 帮助文案（video + web）
tests/data/weevents/index.html  → 新增：回显 fps/paused 到 document.title
tests/integration/test_web.c    → 改：两个集成用例（有/无 fps、pause/DPMS）
docs/specs/we-web-events.md     → 本文件
```

## Code Style

```c
/* 单一来源：dpms 与用户暂停任一为真即暂停。页面没定义回调时静默跳过；
 * 页面可能在 document-start 之后才定义 listener，因此保留短重试。 */
static void web_notify_listener_state(WebCtx *ctx) {
  snprintf(js, sizeof(js),
           "(function(){var n=0;function a(){var L=window.wallpaperPropertyListener;"
           "if(!L){if(++n<50)setTimeout(a,200);return;}"
           "%s"
           "try{if(L.setPaused)L.setPaused(%s);}catch(e){}}a();})();",
           gen, paused ? "true" : "false");
}
```

## Testing Strategy

- 集成（`make test-xvfb`，`tests/integration/test_web.c`）：fixture 页把状态写进
  `document.title`，断言 `GEN:24:false` → pause → `GEN:24:true` → resume → false；
  DPMS `set_paused(1/0)` 同样；无 `--fps` 的实例保持 `GEN:none:false`。
- 单元：现有 `test_web_props` 覆盖发射器；本任务不新增纯逻辑单测。
- 音频：沿用 `tests/integration/test_web_audio.c`（频率是时间敏感项，不做硬断言）。

## Boundaries

- Always: 页面未定义回调时静默跳过；migration/导航后重投当前状态；`make test` +
  `test-xvfb` 通过。
- Ask first: 改变 `--fps` 语义（例如真的包装 rAF）；全局 FPS 默认值。
- Never: 因暂停改动媒体播放状态之外的东西；在 `quiet` 时调用 `setPaused`；给 CLI
  链接新库。

## Success Criteria

- [ ] `--fps 24` → 页面收到 `{fps:24}`；未设置 → 不调用 `applyGeneralProperties`
- [ ] pause/resume、DPMS 冻结/恢复 → `setPaused(true/false)` 正确
- [ ] 导航/reload 后重新投递当前 fps 与暂停状态
- [ ] 音频推送周期 33ms，无监听器零采集
- [ ] `make test` / `make test-xvfb`（除既有 test_video 环境失败）/ `make check-client-libs`
