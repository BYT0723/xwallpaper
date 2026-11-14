# Spec: we-props-combo-text（combo 覆盖值补 `.text`）

## Objective

WE 的 `applyUserProperties` 对 combo 属性投递 `{value, text}`：`text` 是当前选中项的
显示标签（已本地化）。xwallpaper 目前只发 `value`，于是依赖 `.text` 的页面在
`--set-property` 后显示空标签。默认值路径由页面自取 project.json，标签可从
`options` 自行解析；本任务只补 native 发射的覆盖值。

User stories:
- `xwallpaper --set-property bgm=1` 后页面收到 `{value:"1", text:"雨"}`（按 `LANG` 本地化）；
- 选项没有匹配标签/不是 combo 时不发 `text` 字段，页面行为不变；
- 覆盖值重放（reload/daemon 重启）后 `text` 仍正确。

非目标：属性面板 UI；`options` 的其它元数据（order/icon）；非 combo 类型的 text。

## Tech Stack

C11 + json-c；`web_props` 保持纯逻辑可单测；`web.c` 缓存本次 project.json 原文
（≤1 MiB，已有读取上限）用于覆盖值反查标签。

## Commands

```
Build:      make
Test:       make test
Integ(X11): make test-xvfb
Dep guard:  make check-client-libs
```

## Project Structure

```
src/web_props.[ch]        → 改：XWWebProp.text；parse 填充；combo_text 查询；JS 发射
src/web.c                 → 改：缓存 project.json；set/重放时补 text；释放
tests/test_web_props.c    → 改：parse/查询/JS 三组断言
tests/data/wecombos/      → 新增 fixture（combo + 回显页）
tests/integration/test_web.c → 改：set 后断言 text
docs/specs/we-props-combo-text.md → 本文件
```

## Code Style

```c
/* 覆盖值只有 value；text 按 project.json 的 options + localization 反查。 */
int xw_web_props_combo_text(const char *json, size_t len, const char *locale,
                            const char *name, const char *value,
                            char *out, size_t n);
```

## Testing Strategy

- 单测：parse 填充 text（zh-chs 命中、en-us 回退）；`combo_text` 命中/未知值/非 combo/
  参数错误；`object_js` 逐字断言 `{"v":{"value":"1","text":"雨"}}` 且长度函数一致；
  无 text 时仍只发 value。
- 集成：`tests/data/wecombos` 页面回显 JSON；`--set-property bgm=1` 后 title 含
  `"bgm":{"value":"1","text":"Rain"}`。

## Boundaries

- Always: 只对 combo 且标签非空时发 `text`；标签做 JS 转义；无 project.json 时不发。
- Ask first: 把 options 全量投递给页面（WE 不投递）。
- Never: 改动 `value` 的类型规则（bool/number 由前一任务锁定）；持久化 text（每次反查）。

## Success Criteria

- [ ] combo 覆盖值同时带 `value` 与本地化 `text`
- [ ] 未知值/非 combo/无 project.json 时不发 `text`，无行为变化
- [ ] reload 与磁盘恢复后的重放仍带 `text`
- [ ] `make test` / `make test-xvfb` / `make check-client-libs` 绿
