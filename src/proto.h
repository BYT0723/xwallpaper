#ifndef XW_PROTO_H
#define XW_PROTO_H

#include <stddef.h>

#include "cli.h"

/* ------------------------------------------------------------------ *
 *  xwallpaper daemon/client protocol
 *
 *  Messages are one line: LF-terminated, NUL-separated fields.
 *  Paths may contain spaces (a field ends at NUL) but not NUL/LF.
 *
 *  client -> daemon:
 *    set\0<target>\0<type>\0<path>\0[scale=..]\0[mute=1]\0[volume=50]\0
 *        [opacity=0.5]\0[below=1]\0[sticky=1]\0[skip_taskbar=1]\0
 *        [skip_pager=1]\0[no_focus=1]\0[geom=WxH+X+Y]\n
 *      target: all | <output-name> | geom
 *      type:   image | video | web | scene
 *    clear\0<target>\n       target: all | <output-name>
 *      optional field keep=1: remove windows but retain their last state
 *      (screen full-wallpaper clearing; `restore` brings them back)
 *    list-props\0<target>\n  target: primary | <output-name>
 *    set-prop\0<target>\0<name=value>\n
 *    list\n
 *    restore\n              rebuild windows kept by `clear --keep`
 *    exit\n
 *
 *  daemon -> client:
 *    ok\n
 *    error\0<msg>\n
 *    list\0<n>\n  then n lines: <output>\0<type>\0<path>\0<rotation>\n
 * ------------------------------------------------------------------ */

#define XW_SOCKET_DIR_ENV   "XDG_RUNTIME_DIR"
#define XW_SOCKET_SUBDIR    "xwallpaper"
#define XW_SOCKET_NAME      "xwallpaper.sock"

#define XW_MSG_MAX   8192
#define XW_REPLY_MAX 65536

/* Socket path: $XDG_RUNTIME_DIR/xwallpaper/xwallpaper.sock,
 * or /tmp/xwallpaper/xwallpaper.sock if the env var is unset. */
void xw_daemon_socket_path(char *out, size_t n);

/* Encode commands from a config. Returns bytes written or -1. */
int xw_proto_encode_set(const XWConfig *cfg, char *msg, size_t n);
int xw_proto_encode_clear(const XWConfig *cfg, char *msg, size_t n);
int xw_proto_encode_list(char *msg, size_t n);
int xw_proto_encode_list_props(const XWConfig *cfg, char *msg, size_t n);
int xw_proto_encode_set_prop(const XWConfig *cfg, char *msg, size_t n);
int xw_proto_encode_control(const XWConfig *cfg, char *msg, size_t n);
int xw_proto_encode_restore(char *msg, size_t n);int xw_proto_encode_exit(char *msg, size_t n);

#endif /* XW_PROTO_H */
