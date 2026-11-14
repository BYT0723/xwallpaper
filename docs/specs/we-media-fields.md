# Spec: we-media-fields (WE 媒体属性补全)

## Objective

补齐 `xw_we_media_format_props()` 缺少的 Wallpaper Engine
`MediaPropertiesListener` 字段：`subtitle`、`genres`、`albumArtist`。
`we-media-listener` 首版只交付 `title/artist/albumTitle/contentType`，
依赖副标题、流派、专辑艺术家的壁纸拿不到数据。

数据仍来自 MPRIS `Metadata`（`a{sv}`），只读，不新增控制能力：

| MPRIS | 类型 | WE |
|---|---|---|
| `xesam:subtitle` | `s`（可缺失） | `subtitle` |
| `xesam:genre` | `as` | `genres`，多值以 `", "` 连接 |
| `xesam:albumArtist` | `as`，兼容 `s` | `albumArtist` |

User stories: 播客/现场专辑显示副标题；壁纸按流派切换配色；合辑区分专辑与
曲目艺术家。缺失任何键时三个字段都是空字符串；JSON 结构与现在一致，页面不报错。

## Tech Stack

- C11，daemon 侧 `src/we_media.[ch]` + GLib/GIO 的 `GVariant`；不新增依赖，
  不改 CLI/协议/`web.c`/`state.c`/`Makefile`。
- 复用 `lookup_value()`、`copy_utf8()`、`xw_we_media_json_escape()`。

## Commands

```
Build:      make
Unit:       make test
Integ:      make test-xvfb
CLI guard:  make check-client-libs
Manual:     xwallpaperd & 然后 xwallpaper set ~/.../3747222633
```

## Project Structure

```
docs/specs/we-media-fields.md   本 spec
src/we_media.h                  XWMediaInfo 增加 subtitle/genres/album_artist
src/we_media.c                  MPRIS 解析、", " 连接、props JSON、快照同步
tests/test_we_media.c           fixture 扩展 + 解析/格式化精确断言
```

## Code Style

```c
/* 新增字段与现有文本字段同宽同语义 */
typedef struct {
  char title[XW_WE_MEDIA_TEXT_MAX];
  /* ... */
  char subtitle[XW_WE_MEDIA_TEXT_MAX];
  char genres[XW_WE_MEDIA_TEXT_MAX];
  char album_artist[XW_WE_MEDIA_TEXT_MAX];
  char content_type[32];
} XWMediaInfo;

/* `as` -> ", " 连接；跳过非字符串与空串 */
static void join_string_array(GVariant *v, char *out, size_t n);
```

- 新 JSON 键追加在 `contentType` 之后；每个文本字段按
  `XW_WE_MEDIA_TEXT_MAX * 6 + 1` 预留最坏转义空间。
- 不加注释，除非解释 MPRIS 非直观的类型差异（`as` vs `s`）。

## Testing Strategy

`tests/test_we_media.c`（`make test`，无 X/D-Bus）：

1. 扩展 `metadata_full()`：`xesam:subtitle`、`xesam:genre`（含空串元素）、
   `xesam:albumArtist` 数组。
2. 断言 `subtitle`、`genres`（`"Rock, Indie"`）、`album_artist`
   （`"AA, BB"`）以及 `format_props()` 的完整 JSON 片段。
3. `xesam:albumArtist` 为单字符串 `s` 时同样可解析。
4. 空 `a{sv}` 清空三个字段；全部字段填 `0x01` 的最坏转义下不截断。
5. 先写测试确认 RED 再实现至 GREEN，不删既有断言；集成层只透传 props JSON。

## Boundaries

- Always: 保持 JSON 向后兼容；缺失键降级为空串；`make test` 与
  `make check-client-libs` 全绿后再提交。
- Ask first: 改 JSON 键顺序或重命名既有键；把 `genres` 改成数组类型；
  新增依赖；支持 `xesam:comment`、`xesam:lyricist` 等额外字段。
- Never: 把 GIO/GDBus 链进 CLI；为过测试删断言；拼接未转义字符串；截断 UTF-8。

## 非目标

- 不实现 `primaryColor` / `textColor` 取色。
- 不新增播放控制、远程封面下载或缓存；不改 JS shim API。
- 不改变 `contentType` 推断与 `thumbnail` 行为。

## Success Criteria

1. `format_props()` 输出包含 `subtitle`、`genres`、`albumArtist`，顺序稳定；
   最坏转义下不截断、不产生非法 JSON。
2. `xesam:genre` / `xesam:albumArtist` 支持 `as`，后者兼容 `s`；
   空串元素被忽略，多值以 `", "` 连接。
3. 三个键缺失时字段为空串，页面不报错；CLI 链接面不变。
4. `make test`、`make check-client-libs` 通过。
