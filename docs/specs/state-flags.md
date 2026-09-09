# Spec: persist backend flags in state file

## Objective

daemon 重启 restore / `clear --keep` + `restore` 时，壁纸只恢复了
type/path/rotation，`--fps --mute --volume --keybinds --audio-client-name
--scale -o -b -s -st -sp -nf` 全部丢失（`restore_entry` 里硬编码
scale=FILL、volume=100、opacity=1.0、video mute=1）。
目标：set 时的全部 flags 落盘，恢复时原样重建。

## Decision

- 快照点：window 结构随身携带 flags（set/create/reload 时从 `cfg/opt`
  拷贝），`entry_from_index` 原样抄进 `XWStateEntry`，restore 时回填 `cfg`。
- 行格式向后追加 12 字段（keep 之后）：
  `keep\tmute\tvolume\tfps\tscale\topacity\tbelow\tsticky\tskip_taskbar\tskip_pager\tno_focus\tkeybinds\taudio`
  - scalars：`mute volume fps scale opacity below sticky skip_taskbar skip_pager no_focus`
  - strings（走现有 escape）：`keybinds audio_client_name`
- 旧 6 字段行照常解析，缺失字段回退今日行为：
  scale=FILL、volume=100、fps=0、opacity=1.0、below/sticky/…=0、
  keybinds=""、acl="xwallpaper"、mute 用哨兵 -1（缺失时 video→1、
  其它→0，与今日硬编码一致）。
- mute 存 -1/0/1？行格式里 mute 正常只写 0/1；entry 内存里 -1 仅表示
  "旧行缺失"。format 永远写 0/1（-1 规范化为今日等价值：video→1）。
- opacity 在 Wayland 下当前无消费，仅记录（X11 restore 保真）。

## Testing Strategy

- 单测（`tests/test_state.c`，纯逻辑无需 X）：
  RED→GREEN：12 字段 round-trip；旧 6 字段行解析出 legacy 默认
  （重点：video mute 哨兵行为）。
- 集成（`tests/integration/test_restore.c`，xvfb）：看现有覆盖，
  能加则加 "带 flags 的 set → state 文件含新字段" 断言。
- 全门禁：`make test && make test-xvfb && make check-client-libs`。

## Boundaries

- Always: 四处 set 路径（X11 create/rebuild/reload + wl attach/rebuild/reload）
  全部记录 flags，不留"新 set 丢 flags"的洞；旧行解析零变化。
- Never: 不改前 6 字段顺序/语义；不动 socket 协议（daemon 内 cfg 已全）；
  不碰 opacity 在 X11 窗口创建的消费逻辑。

## Success Criteria

1. `set --video --fps 10 --mute --volume 30 a.mp4` → state 行含 12 新字段。
2. daemon 重启后恢复的视频仍是 fps=10/mute/volume=30（mpv 生效）。
3. 旧 state 文件（6 字段）恢复行为与今天完全一致。
4. 全门禁绿。
