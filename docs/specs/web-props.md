# Spec: web-props (web 壁纸 user-property 桥接)

## Objective

给 web 后端接上 `--list-properties / --set-property`（与 scene 后端同契约），
让 WE web 壁纸（如 `3818569421` 棕色尘埃2立绘播放器）的 `project.json`
`general.properties` 可在运行时查看和修改：背景色、角色缩放/位置、音量、
悬浮球/面板配色等。解决"白底小人卡死在默认值、用户调不动"的问题。

User stories:
- `xwallpaper set --web <BD2目录>` 后白底小人 → `xwallpaper --list-properties`
  看到 `bgColor/color/1.0 1.0 1.0`、`roleScale/slider/100` 等 →
  `xwallpaper --set-property bgColor="0 0.2 0"` 背景变绿，
  `--set-property roleScale=200` 人物变大（已用 harness 验证页面端映射可用）。
- daemon 重启/`restore`、页面内导航后，已 set 的覆盖值仍生效（内存态重放）。
- 外网 URL 壁纸（无 project.json）行为不变：`--list-properties` 报
  "that wallpaper has no user properties"。

非目标：持久化覆盖值到磁盘（与 scene 对齐：内存态；该 BD2 页自己写
localStorage 是页面行为，不依赖）；WE 属性面板 UI；改 protocol 文本格式。

## Tech Stack

- C11，`webkit2gtk-4.1 + GTK3`，`json-c`（daemon 已有依赖，`scene_parse.c`
  已无条件使用，不新增构建依赖）。
- 沿用 `web_control` 的同线程 `evaluate_javascript` 模式调
  `window.wallpaperPropertyListener.applyUserProperties`（daemon 命令处理线程
  即 GTK 主线程语境，`web_control` 已是先例）。

## Commands

```
Build:      make
Test:       make test
Integ(X11): make test-xvfb
Dep guard:  make check-client-libs
Manual smoke:
  ./xwallpaperd &
  ./xwallpaper set -g 1280x720+0+0 --name w1 --web ~/wall/bd2
  ./xwallpaper --list-properties
  ./xwallpaper --set-property bgColor="0 0.5 0"
  ./xwallpaper --set-property roleScale=200
  ./xwallpaper clear -m w1
  ./xwallpaper exit
```

## Project Structure

```
src/web.c                        → 改：WebCtx 缓存 props+overrides；新增
                                   list_props/set_prop；LOAD_FINISHED 后重放
                                   overrides；init/reload 时解析 project.json
src/web_props.[ch] (new)         → 新增：纯逻辑（project.json 解析、JS 字符串
                                   转义、applyUserProperties 片段构造），可单测
tests/test_web_props.c (new)     → 新增单测（纯逻辑，无 X）
tests/integration/test_web.c     → 扩展：list/set 集成用例
tests/data/wepropset/ (new)      → 新增 fixture：project.json（2~3 个不同类型
                                   属性）+ 监听回显页
docs/specs/web-props.md          → 本文件
```

## Code Style

```c
/* web_props.h — 纯逻辑，无 GTK/WebKit/X11 依赖，可单测 */
typedef struct { char name[128]; char type[32]; char value[1024]; } XWWebProp;
int xw_web_props_parse(const char *json, size_t len,
                        XWWebProp *out, int cap);       /* 返回个数，-1=非法 */
int xw_web_props_apply_js(const XWWebProp *props, int n,
                           char *out, size_t m);        /* 构造 JS 片段，0=ok */
/* 命名 xw_<module>_<action>；错误沿用 XWError；daemon 侧 WebCtx 持有缓存 */
```

- set 语义：name 必须存在于 project.json（未知名 → `unknown property`），value
  原串透传（页面端 `fb()` 负责按 type 解析：slider→Number，color→三元组，
  file→路径；与 WE 行为一致）。
- list 输出与 scene 同格式：`name\ttype\tvalue\n`，value 为覆盖后的当前值；
  含 `\t`/`\n` 的条目跳过（与 scene 一致）。
- JS 转义：value 仅出现在单引号 JS 字符串字面量里，转义 `\`、`'`、换行；
  构造 `applyUserProperties({name:"value"})`（页面 `fb()` 同时接受原串与
  `{value}` 对象形式，传对象形式最稳）。

## Testing Strategy

- 单测（无 X，`make test`）：`tests/test_web_props.c` —— project.json 解析
  （color/slider/file/bool、缺 type、非法 JSON、超 cap）、JS 转义（含引号/
  反斜杠/换行）、apply 片段构造。纯逻辑目标 ~90%。
- 集成（Xvfb，`make test-xvfb`）：新 fixture `wepropset`，页面的
  `applyUserProperties` 把收到的值回显到 `document.title`；断言：
  1. `list_props` 返回 fixture 的 3 个属性且值正确；
  2. `set_prop` 未知名 → -1；
  3. `set_prop bg=...` 后 title 回显新值（页面真收到了）；
  4. `reload` 后 overrides 重放（title 仍是新值）。
- 手动：上面 smoke 命令 + 真实 BD2 壁纸截图对比（白底小人 → 绿底大 freely）。
- 回归门：`make check-client-libs` 必须过（新增代码只进 `xwallpaperd`）。

## Boundaries

- Always: `make test` 全绿再合；`destroy` 释放 props 缓存；`reload` 重读
  project.json（覆盖值保留）；`set-prop`  target 解析沿用现有 `props_backend`。
- Ask first: 新增构建依赖（本方案不新增）；持久化覆盖值到 state 文件；
  动 `web_init_wl`（本方案 X11/Wayland 共用 `WebCtx` 缓存 + 同一重放点，
  Wayland 只需确认 `load-changed` 同样触发——实现时核对）。
- Never: fork 外部进程；绑端口；webkit/mpv 进 CLI；提交 secret；改 protocol 格式。

## Success Criteria

1. `--list-properties` 对目录 web 壁纸列出 project.json 全部属性（名/类型/当前值）。
2. `--set-property roleScale=200` 后 BD2 人物变大、`bgColor="0 1 0"` 后背景变绿
  （集成测试用 title 回显 + 手动截图双重验证）。
3. `reload` 后覆盖值仍生效；未知属性名返回可读错误。
4. `http(s)` 壁纸 `--list-properties` 仍报 "no user properties"，行为不变。
5. `make test`、`make test-xvfb`、`make check-client-libs` 全过。

## Open Questions

- 无（已定：value 原串透传由页面解析；覆盖值内存态与 scene 对齐；
  volume 属性走页面内音频链，与 `web_control` 的 master 正交，互不干扰）。
