# Spec: we-api-stubs（未实现的 WE 全局 API 占位）

## Objective

部分 WE web 壁纸会无条件访问一些 xwallpaper 不提供的全局符号，导致脚本在加载阶段抛
异常、整张壁纸黑屏。为这些符号提供**语义正确的最小占位**：

- `window.wallpaperReady`：WE 内部使用的就绪回调，页面可能调用；no-op。
- `window.wpPlugins.led.setAllDevicesByImageData(data, w, h)`：RGB 插件入口；接受参数并
  丢弃，返回 `true`。

明确**不伪造**的能力：

- 不调用 `wallpaperPluginListener.onPluginLoaded('led', ...)`：我们没有 LED 输出，声称为
  “已加载”只会让壁纸白跑每帧取色。
- 不定义 `window.cue`：WE 只在 iCUE 插件存在时定义它，未安装 iCUE 的 WE 同样是
  `undefined`，页面应自行特性检测。

非目标：真正的 RGB 输出（OpenRGB，另行评估）；`wallpaperPluginListener` 的其它插件。

## Tech Stack

C11 + document-start 注入的 JS shim（`web.c`），无新依赖。

## Commands

```
Build:      make
Test:       make test
Integ(X11): make test-xvfb
Dep guard:  make check-client-libs
```

## Project Structure

```
src/web.c                     → 改：新增 kStubJS 并在 document-start 注入
tests/data/weapi/index.html   → 新增：回显 typeof/调用结果到 title
tests/integration/test_web.c  → 改：一个集成用例
docs/specs/we-api-stubs.md    → 本文件
```

## Code Style

```js
window.wallpaperReady = window.wallpaperReady || function () {};
window.wpPlugins = window.wpPlugins || {};
window.wpPlugins.led = window.wpPlugins.led || {
  setAllDevicesByImageData: function () { return true; }
};
```

## Testing Strategy

- 集成：fixture 页把
  `typeof wallpaperReady : typeof wpPlugins : typeof wpPlugins.led.setAllDevicesByImageData :
   typeof window.cue : 调用返回值` 写进 title，断言
  `API:function:object:function:undefined:true`。
- 单测：无纯逻辑。

## Boundaries

- Always: 只在缺失时定义，不覆盖页面自己的实现；不产生任何硬件访问。
- Ask first: 增加其它插件占位（如 cue 的方法）。
- Never: 伪造 `onPluginLoaded`；让占位函数抛异常或阻塞。

## Success Criteria

- [ ] `wallpaperReady` 可调用且 no-op
- [ ] `wpPlugins.led.setAllDevicesByImageData` 可调用、返回 true、不报错
- [ ] `window.cue` 保持 undefined（与无 iCUE 的 WE 一致）
- [ ] 页面原有 shim（音频/媒体/properties）无回归
- [ ] `make test` / `make test-xvfb` / `make check-client-libs` 绿
