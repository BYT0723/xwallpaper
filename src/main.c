#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "cli.h"
#include "client.h"
#include "proto.h"

int main(int argc, char **argv) {
  /* library code (mpv etc.) needs the C numeric locale to parse decimals */
  setlocale(LC_ALL, "");
  setlocale(LC_NUMERIC, "C");

  XWConfig cfg;
  XWError err;
  memset(&err, 0, sizeof(err));

  if (xw_cli_parse(argc, argv, &cfg, &err) != 0) {
    fprintf(stderr, "xwallpaper: %s\n", err.msg);
    xw_cli_usage(stderr);
    return 1;
  }

  if (cfg.command == XW_CMD_DAEMON) {
    fprintf(stderr, "xwallpaper: run 'xwallpaperd' instead (daemon is no longer a client command)\n");
    return 1;
  }

  /* encode the command */
  char msg[XW_MSG_MAX];
  int n = -1;
  switch (cfg.command) {
  case XW_CMD_SET:     n = xw_proto_encode_set(&cfg, msg, sizeof(msg)); break;
  case XW_CMD_CLEAR:   n = xw_proto_encode_clear(&cfg, msg, sizeof(msg)); break;
  case XW_CMD_LIST:
  case XW_CMD_STATE:   n = xw_proto_encode_list(msg, sizeof(msg)); break;
  case XW_CMD_RESTORE: n = xw_proto_encode_restore(msg, sizeof(msg)); break;
  case XW_CMD_EXIT:    n = xw_proto_encode_exit(msg, sizeof(msg)); break;
  default:
    fprintf(stderr, "xwallpaper: no command given\n");
    xw_cli_usage(stderr);
    return 1;
  }
  if (n < 0) {
    fprintf(stderr, "xwallpaper: command too long\n");
    return 1;
  }

  /* connect to the daemon (must be running as xwallpaperd), send, print reply */
  int fd = xw_client_connect(&err);
  if (fd < 0) {
    fprintf(stderr, "xwallpaper: %s\n", err.msg);
    return 1;
  }

  char reply[XW_REPLY_MAX];
  int r = xw_client_talk(fd, msg, (size_t)n, reply, sizeof(reply), &err);
  if (r < 0) {
    fprintf(stderr, "xwallpaper: %s\n", err.msg);
    close(fd);
    return 1;
  }

  /* print the reply for list/state commands */
  if (cfg.command == XW_CMD_LIST || cfg.command == XW_CMD_STATE) {
    /* reply starts "list\0N\n" then N window lines: <name>\0<type>\0<path>\0<rot>\n */
    /* --list prints the first field (name) only; --state prints all four TAB-separated */
    int fields = (cfg.command == XW_CMD_LIST) ? 1 : 4;
    const char *p = reply;
    const char *nl = memchr(p, '\n', XW_REPLY_MAX);
    if (nl && strncmp(p, "list", 4) == 0) {
      const char *rest = nl + 1;
      while (*rest && rest < reply + XW_REPLY_MAX) {
        const char *line_end = memchr(rest, '\n', XW_REPLY_MAX - (size_t)(rest - reply));
        if (!line_end)
          break;
        const char *f = rest;
        for (int field = 0; field < fields; field++) {
          const char *fe = memchr(f, '\0', (size_t)(line_end - f));
          size_t len = fe ? (size_t)(fe - f) : (size_t)(line_end - f);
          if (field > 0)
            putchar('\t');
          fwrite(f, 1, len, stdout);
          if (!fe)
            break;
          f = fe + 1;
        }
        putchar('\n');
        rest = line_end + 1;
      }
    } else {
      printf("%s", reply);
    }
  }
  /* set/clear/exit print nothing on success; exit code carries the result */

  close(fd);
  return 0;
}
