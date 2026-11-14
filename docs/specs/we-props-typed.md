# Spec: we-props-typed（覆盖值按 WE 类型发射）

## Objective

修复 WE web 壁纸的 user property **覆盖值**类型退化：`--set-property` 与 daemon 重放
的覆盖值目前一律拼成 `{"value":"..."}`，于是 bool 变成 `"true"`、slider 变成
`"100"`。WE 页面常用 `properties.x.value === true`、`.toFixed(2)`、`Number.isInteger`，
这些写法在覆盖后失效。

现状范围（已核实）：页面加载时自己 `fetch('project.json')`，默认值类型正确；只有
native 发射的覆盖值受影响（`web_set_prop`、`web_clear_props` 重放、`LOAD_FINISHED`
后的覆盖重放）。

User stories:
- 我 `xwallpaper --set-property speed=150` 后，页面里 `typeof speed.value === 'number'`，
  `speed.value.toFixed(0)` 不抛异常。
- 我 `--set-property enabled=true`（bool 属性）后，`enabled.value === true` 成立。
- 我 `--set-property bgColor="0 0.2 0"` 后，color 仍是字符串三元组，页面 `split(' ')`
  照常工作。
- 页面里 `properties.x.text`（combo 选中项文本）不在本 spec 范围（见非目标）。

## Tech Stack

C11；`json-c` 仅用于解析 project.json；发射器是纯字符串拼接（无 JS 引擎）。
单测沿用 `tests/harness.h`，不链 GTK/WebKit/X11。

## Commands

```
Build:      make
Test:       make test
Integ(X11): make test-xvfb
Dep guard:  make check-client-libs
```

## Project Structure

```
src/web_props.c        → 改：值发射按 type 分支；长度计算同步
src/web_props.h        → 改：apply_js/object_js 注释说明类型规则
tests/test_web_props.c → 改：逐字断言 bool/number/string 三种形态 + 非法值回退
docs/specs/we-props-typed.md → 本文件
```

## Code Style

```c
/* 仅 bool 的 "true"/"false" 与合法 JSON 数字裸发；其余一律字符串。 */
static int prop_value_is_raw(const XWWebProp *p) {
  if (strcmp(p->type, "bool") == 0)
    return strcmp(p->value, "true") == 0 || strcmp(p->value, "false") == 0;
  if (strcmp(p->type, "slider") == 0)
    return is_json_number(p->value);
  return 0;
}
```

## Testing Strategy

- 单测（`make test`，纯逻辑）：逐字锁死 JS 字面量。
  - slider `"200"` → `{"roleScale":{"value":200}}`
  - bool `"true"`/`"false"` → 裸值；`"TRUE"` → 回退字符串
  - 数字校验：`"0012"`、`" 200"`、`"200 "`、`"abc"`、`"inf"`、`"0x10"` → 字符串回退
  - color/combo/text/file/directory → 保持字符串 + 原有转义
  - `xw_web_props_object_js_len()` 与实际输出长度严格一致
- 集成（`make test-xvfb`）：本任务不加新 fixture；类型正确性由单测覆盖（集成路径
  与默认值投递共用同一发射器）。

## Boundaries

- Always: `make test` 通过后才提交；保持 `--list-properties` TSV 原串不变；非法值回退
  字符串而不是产出坏 JS。
- Ask first: 改动 `XWWebProp` 结构体布局或 `xw_web_props_*` 公共签名（本任务不需要）。
- Never: 改页面默认值的投递路径（`fetch('project.json')`）；为 combo `.text` 引入
  options 存储；改协议/state/CLI。

## Non-goals

- combo 覆盖值的 `.text`（需要保存 options + 深释放 API，另立任务）。
- 页面加载时的默认值类型（由页面自己读 project.json，已正确）。
- slider 范围（min/max/step）校验：WE 也只在 UI 侧约束，CLI 原样透传。

## Success Criteria

- [ ] slider/bool 覆盖值以原生 JSON 类型到达页面（单测逐字锁定）
- [ ] 非法值一律回退字符串，输出始终是合法 JS
- [ ] `xw_web_props_object_js_len()` 与实际输出一致（含转义/裸值）
- [ ] `make test` / `make check-client-libs` 绿
- [ ] 无协议、state、CLI、Makefile 变更
