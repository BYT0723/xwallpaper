# Spec: we-media-colors (媒体封面取色)

## Objective

为 Wallpaper Engine 的 `wallpaperRegisterMediaThumbnailListener` 补全封面取色字段：
`primaryColor`、`secondaryColor`、`tertiaryColor`、`textColor`、
`highContrastColor`。此前只发送 `primaryColor:""` / `textColor:""`，依赖这些字段
做主题色的壁纸无法工作。

数据源是已经解析到的封面（本地 `file://`、绝对路径、`data:` 内嵌图片）；只从像素
统计颜色，不改变封面交付路径。远程封面或解码失败时字段为空，行为与以前一致。

User stories:
- 本地/内嵌封面的壁纸能拿到与封面一致的主色和可读的文字色。
- 封面解码失败、纯透明或远程 URL 时不报错，五个字段为空。
- 多张 web 壁纸共享同一份取色结果，页面不再自行计算。

## Tech Stack

- C11；新增纯模块 `src/we_color.[ch]`，不依赖 GLib/GTK/WebKit，可无 X 单测。
- 解码使用 daemon 已有依赖 gdk-pixbuf（由 gtk+-3.0 引入）：
  `gdk_pixbuf_new_from_file_at_scale`、`gdk_pixbuf_new_from_stream_at_scale`。
- `xwallpaper` CLI 仍只链接 libc；不新增 CLI 参数、线程或同步网络请求。

## Commands

```
Build:      make
Unit:       make test
Integ:      make test-xvfb
CLI guard:  make check-client-libs
```

## Project Structure

```
src/we_color.h/.c        4bit 量化、聚类、对比色选择（纯函数）
src/we_media.h           XWMediaInfo 增加 color_* 字段
src/we_media.c           封面解码、取色、快照传播、thumbnail 载荷
tests/test_we_color.c    纯取色单测
tests/test_we_media.c    封面解码 + 载荷单测
Makefile                 模块、单测链接（gdk-pixbuf/gio/-lm）
README.md                能力说明
```

## Code Style

```c
int xw_we_color_palette(const unsigned char *px, int w, int h, int rowstride,
                        int n_channels, char primary[8], char secondary[8],
                        char tertiary[8], char text[8], char high[8]);
```

- 输出固定 `#rrggbb` 小写；没有对应簇时写空字符串。
- 每通道取高 4 bit 量化（4096 桶）；按 `max(1, w/64)`、`max(1, h/64)` 采样降采样。
- 4 通道且 alpha==0 的像素忽略；rowstride padding 不参与统计。
- 候选按像素数降序、同数量按相对亮度降序；RGB 欧氏距离平方 `> 32²` 才算不同簇。
- `text`/`high` 用 WCAG 相对亮度，在白/黑中选对比度更高者；无主色时全为空。
- 命名 `xw_we_color_*`；纯函数不打印日志、不保存跨调用状态。

## Testing Strategy

### 单测（`make test`）

- `tests/test_we_color.c`：纯红/纯黑、全透明、双色/三色、近似色、rowstride padding、
  非法参数。
- `tests/test_we_media.c`：`tests/data/red.png` 与 base64 `data:` URL 的取色断言；
  http(s) 保持空；格式化载荷包含五个颜色字段。

### 手动

本地封面歌曲 → 壁纸主题色随封面变化；远程封面 → 颜色字段为空；切歌、暂停、清空
后颜色随快照一起更新。

## Boundaries

- Always: 取色失败降级为空；不阻塞主循环；保持现有封面 data URL 行为；
  `make test`、`make check-client-libs` 全过再合。
- Ask first: 新增图像解码依赖；下载远程封面；把 gdk-pixbuf 链进 CLI。
- Never: 在纯模块里引入 GLib/GTK；修改 `src/web.c` 的推送协议；为过测试删断言。

## Non-goals

- 不下载/缓存 `http(s)` 封面，因此远程封面的颜色保持空。
- 不做 ICC、色彩空间或 gamma 校正；按 8-bit sRGB 处理。
- 不处理 Wallpaper Engine 的 scene/SceneScript 或任何非 web 壁纸。
- 不做调色板缓存或跨曲目复用。

## Success Criteria

1. 本地或 `data:` 封面可解码时，thumbnail 回调同时收到五个颜色字段。
2. 纯红 1x1/8x8 → `#ff0000`；纯黑 → `#000000` 且文字色 `#ffffff`；全透明 → 全空。
3. 远程或解码失败时封面照常交付，五个颜色字段为空。
4. 切歌、清空、停止后快照中的颜色同步清空。
5. `make test` 与 `make check-client-libs` 全绿。
