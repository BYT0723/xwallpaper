#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "cli.h"
#include "client.h"
#include "daemon.h"

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
    return xw_daemon_main();
  }

  /* encode the command */
  char msg[XW_MSG_MAX];
  int n = -1;
  switch (cfg.command) {
  case XW_CMD_SET:   n = xw_proto_encode_set(&cfg, msg, sizeof(msg)); break;
  case XW_CMD_CLEAR: n = xw_proto_encode_clear(&cfg, msg, sizeof(msg)); break;
  case XW_CMD_LIST:  n = xw_proto_encode_list(msg, sizeof(msg)); break;
  case XW_CMD_EXIT:  n = xw_proto_encode_exit(msg, sizeof(msg)); break;
  default:
    fprintf(stderr, "xwallpaper: no command given\n");
    xw_cli_usage(stderr);
    return 1;
  }
  if (n < 0) {
    fprintf(stderr, "xwallpaper: command too long\n");
    return 1;
  }

  /* connect (spawning the daemon on first use), send, print reply */
  int fd = xw_client_connect(1, &err);
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

  /* print list output if this was a list command */
  if (cfg.command == XW_CMD_LIST) {
    /* reply starts "list\0N\n" then N window lines */
    const char *p = reply;
    const char *nl = memchr(p, '\n', XW_REPLY_MAX);
    if (nl && strncmp(p, "list", 4) == 0) {
      /* skip header, print remaining lines */
      const char *rest = nl + 1;
      while (*rest && rest < reply + XW_REPLY_MAX) {
        const char *line_end = memchr(rest, '\n', XW_REPLY_MAX - (size_t)(rest - reply));
        if (!line_end)
          break;
        /* print the output name (first field) */
        const char *field_end = memchr(rest, '\0', (size_t)(line_end - rest));
        size_t name_len = field_end ? (size_t)(field_end - rest) : (size_t)(line_end - rest);
        printf("%.*s\n", (int)name_len, rest);
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
