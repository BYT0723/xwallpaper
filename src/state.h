#ifndef XW_STATE_H
#define XW_STATE_H

#include <stddef.h>

#include "backend.h"

/* Persistent per-name wallpaper state. xwallpaper 持久化每个 name (xrandr
 * output 名或 -g --name) 的当前壁纸, 以便 daemon 重启后自动恢复。
 *
 * 状态文件行格式 (TAB 分隔, 一行一个窗口):
 *   name\ttype\tpath\trotation\tgeom\tkeep
 *   - name:     窗口身份 (xrandr output 名, 或 -g 的 --name 如 grp_xxx / Screen)
 *   - type:     image | video | web
 *   - path:     壁纸文件路径
 *   - rotation: 视频旋转角度 0/90/180/270
 *   - geom:     WxH+X+Y, 或 '-' 表示 name 是 xrandr output (恢复时重解析几何)
 *   - keep:     0 = 当前窗口 (daemon 启动时恢复);
 *               1 = last 保留 (clear --keep 后窗口已清, 由 restore 命令恢复)
 *
 * name / path 字段内的 '\t' '\n' '\r' '\\' 会被转义为 \\t \\n \\r \\\\,
 * 保证分隔符与换行不会被路径内容破坏。字段超长时该行拒绝 (不截断半截路径)。
 */

#define XW_STATE_NAME_LEN 64
#define XW_STATE_PATH_LEN 1024
#define XW_STATE_MAX_ENTRIES 64
/* 状态行缓冲区: 2x(name+path) 转义 + 其它字段 + NUL */
#define XW_STATE_LINE_MAX (2 * (XW_STATE_PATH_LEN + XW_STATE_NAME_LEN) + 64)

typedef struct {
  char name[XW_STATE_NAME_LEN];
  int type;          /* XW_BACKEND_* */
  char path[XW_STATE_PATH_LEN];
  int rotation;      /* 0/90/180/270 */
  int have_geom;     /* 1 = 用保存的 geom 恢复; 0 = name 是 output, 重解析 */
  int x, y;
  unsigned int w, h;
  int keep;          /* 1 = last 保留 (窗口已清, 待 restore) */
} XWStateEntry;

/* 状态文件路径: $XDG_STATE_HOME/xwallpaper/state,
 * fallback $HOME/.local/state/xwallpaper/state.
 * Wayland 用独立文件 (…/state-wayland): 两平台 monitor 命名空间不一致
 * (xrandr 名 vs wlr 输出名)，共用会导致切平台恢复时贴错屏或回退旧几何。 */
int xw_state_path(char *out, size_t n);

/* 平台分发: use_wayland != 0 → …/state-wayland, 否则与 xw_state_path 同值 */
int xw_state_path_for(int use_wayland, char *out, size_t n);

/* 序列化一行 (含结尾 '\n', 不超 n 字节)。返回字节数, -1 溢出。 */
int xw_state_format_line(const XWStateEntry *e, char *out, size_t n);

/* 解析一行 (可含结尾 '\n')。返回 0 成功, -1 格式错误。 */
int xw_state_parse_line(const char *line, XWStateEntry *e);

/* 原子写整个状态文件 (tmp + rename); entries 为 NULL / count 0 时清空。
 * 自动创建目录。返回 0 成功, -1 失败。 */
int xw_state_save(const char *path, const XWStateEntry *entries, int count);

/* 读入 entries[] (最多 max)。返回 0, *count 置为行数;
 * 文件不存在时 count=0; 解析失败的行跳过。返回 -1 仅表示 IO 错误。 */
int xw_state_load(const char *path, XWStateEntry *entries, int max, int *count);

/* 按 name upsert 一条状态 (替换同名或追加), 原子写。返回 0 成功, -1 失败。 */
int xw_state_upsert(const char *path, const XWStateEntry *e);

#endif /* XW_STATE_H */
