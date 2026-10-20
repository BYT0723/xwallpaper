# Spec: props-persist (web+scene 用户属性落盘持久化)

## Objective

`--set-property` 的覆盖值目前只有 daemon 内存态（`web.c` per-wallpaper 缓存；`scene` 连同壁纸 `reload` 都丢）。
切壁纸回来能记住，但 `xwallpaperd` 重启/`restore` 后回到 `project.json` 默认值，web 背景色等定制丢失。

User stories:
- `xwallpaper set --web ~/wall/bd2` → `--set-property bgColor="0 0.5 0"` → `exit` 重启 daemon → 自动恢复的 web 壁纸仍是绿色，而不是白色默认。
- `A -> B -> A` 切换时 A/B 各自的覆盖互不串味（内存缓存已做到，落盘后保持）。
- `--set-property zoom=200` 后切到 `wepropset2`（同名属性默认 50）看到 50 而不是 200；切回看到 200。
- 用户想回默认值时有明确清理手段：`--clear-properties` 清掉指定壁纸的覆盖（内存+落盘），页面回到 `project.json` 默认。

## Tech Stack

- C11，`json-c`（daemon 已依赖，`scene_parse.c`/`web_props.c` 在用，不新增构建依赖）。
- 复用现有 `backend.list_props/set_prop` 契约做快照/恢复，不改 protocol 文本格式（只新增 `clear-props` verb，按现有 `set-prop` 编码风格）。
- 原子写：`tmp + rename`（与 `state.c` 同模式），文件 `0600`，目录 `0700`。

## Commands

```
Build:      make
Test:       make test
Integ(Xvfb): make test-xvfb
Dep guard:  make check-client-libs
Manual smoke:
  ./xwallpaperd &
  ./xwallpaper set --web ~/wall/bd2
  ./xwallpaper --set-property bgColor="0 0.5 0"
  ./xwallpaper exit
  ./xwallpaperd &
  ./xwallpaper --list-properties   # 仍是 0 0.5 0，而不是默认
  ./xwallpaper --clear-properties
  ./xwallpaper --list-properties   # 回到默认
  ./xwallpaper exit
```

## Project Structure

```
src/props_store.[ch] (new) → 纯逻辑：canonical 身份 -> sha256 key、sidecar 读写
                             （JSON via json-c，原子写，0600），无 GTK/X11，可单测
src/web.c                    → 改：set_prop 写透内存+落盘；init/reload 先内存后落盘恢复；
                             新增 clear_props（清 live+内存+落盘并重放默认值）
src/scene.c                  → 改：同 web；reload 不再丢弃 overrides（先快照再恢复）；
                             新增 clear_props；script 层加 clear/get-all 支持
src/scene_script.[ch]        → 加：xw_script_clear_user_prop(s,name) + 枚举 overrides
src/backend.h                → 加：optional clear_props(b) 槽位（image/video 置 NULL）
src/app.[ch]                 → 加：xw_app_clear_props()；entry restore 后重放 sidecar；
                             save_state 不动（props 按壁纸身份存，不按窗口存）
src/daemon.c                 → 改：set-prop 成功后落盘；新增 clear-props verb；startup/restore
                             走 backend 恢复路径
src/cli.c + proto.[ch]       → 加：--clear-properties + clear-props 编码/解码
src/web.h                    → 暴露测试钩子已有的 cache clear，新增 store 路径可注入（测 tmpdir）
docs/specs/props-persist.md  → 本文件
tests/test_props_store.c     → 单测：key 稳定、读写 roundtrip、坏文件跳过、原子性
tests/integration/test_web.c → 扩展：set 后 destroy 重建仍恢复；clear-props 回默认
tests/integration/test_scene_props.c → 扩展同上
```

身份与路径（ASSUMPTIONS）：
1. 壁纸身份 = web `root_dir`（绝对，`realpath`）/ scene `dir`（绝对）；非 dir（http/svg）无 props，不落盘。
2. sidecar 目录共享平台：`$XDG_STATE_HOME/xwallpaper/props/<sha256>.json`（`HOME/.local/state` fallback），X11/Wayland 共用——同一壁纸换平台仍是同一套定制。
3. 同一壁纸贴多屏共享同一份覆盖（按壁纸身份，不按 output name）。
4. 磁盘是 source of truth 跨重启；内存是 write-through 缓存：`set-prop` 同时写两者；`init/reload` 先看内存， miss 再读盘并回填内存。
5. `project.json` 改了导致某属性消失：恢复时跳过该条（`set_prop` 报 unknown 即跳过），并剪掉内存/落盘中的该条。

## Code Style

```c
/* props_store.h — 纯逻辑，无 GTK/WebKit/X11，可单测 */
int xw_props_store_path(const char *wallpaper_id, char *out, size_t n);
int xw_props_store_save(const char *wallpaper_id,
                        const XWUserProp *props, int n);   /* 原子写，0=ok */
int xw_props_store_load(const char *wallpaper_id,
                        XWUserProp *out, int cap, int *n); /* 不存在返回 1，坏文件返回 -1 并忽略 */
/* 命名 xw_<module>_<action>；错误沿用 XWError；daemon 侧 backend 只调这三件套 */
```

- web color 继续走 `xw_web_props_normalize`（`#rrggbb` 归一化后再落盘，盘上永远是 WE 三元组）。
- scene value 原串透传（与现有 `set_prop` 一致，页面/脚本端解析）。
- 落盘 JSON 形如 `{"v":1,"id":"<root>","props":{"zoom":{"type":"slider","value":"200"}}}`，未知字段忽略向前兼容。

## Testing Strategy

- 单测（无 X，`make test`）：`tests/test_props_store.c` —— key 对同一 `root` 稳定、不同 `root` 不同；save/load roundtrip（含 color 归一化后的值、空集删文件、超 cap 拒绝）；坏 JSON/权限错误返回可预期且不 crash。
- 集成（Xvfb，`make test-xvfb`）：
  1. web：`set zoom=200` → `destroy` → `init` 同一 `root` → `list` 仍 200（落盘恢复）；
  2. web：`clear-props` 后 `list` 回默认 100，且 sidecar 文件消失；
  3. scene：同 1/2（`reload` 不丢）；
  4. 隔离：`wepropset(200)` → `wepropset2(默认50)` → 切回仍 200（内存+落盘一致）。
- 回归门：`make check-client-libs` 必须过（新增只进 `xwallpaperd`）。

## Boundaries

- Always: `make test` 全绿再合；`destroy` 释放；`reload` 重读 `project.json` 后再重放；`set-prop` 非法值大声失败不落盘；sidecar 目录 `0700`/文件 `0600`。
- Ask first: 新增构建依赖；改 protocol 文本格式（本方案只加 verb，不改旧 verb）；动 `state` 行格式；Wayland 分独立 props 目录（本方案共用，需改要先问）。
- Never: fork 外部进程；绑端口；webkit/mpv/imlib2 进 CLI；提交 secret；把用户壁纸路径原文写进可读日志（sidecar JSON 里存 `id` 仅用于调试，可截断）。

## Success Criteria

1. daemon 重启后 web/scene 的 `--set-property` 覆盖仍生效（集成测试 destroy+init 证明）。
2. `A -> B -> A` 各自恢复自己的值，同名属性不互串。
3. `--clear-properties` 后回到 `project.json` 默认，且内存+落盘都清。
4. 非 dir 壁纸（http/svg）行为不变：`list` 报无属性，`set/clear` 可读错误。
5. `make test`、`make test-xvfb`、`make check-client-libs` 全过。

## Open Questions

- sidecar 保留上限：目前不设 TTL/LRU（32 内存上限已有；落盘随壁纸数增长，`clear-props` 可手动清）。是否需要 `xwallpaper --gc-props` 尚待验证，先不做。
- scene 无脚本纯静态壁纸：`set-prop` 本就报无属性，落盘不建文件，与 web 无 props 一致。
