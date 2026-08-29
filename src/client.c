#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "client.h"
#include "proto.h"

static int connect_to(const char *path) {
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path), "%.107s", path);
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(fd);
    return -1;
  }
  /* never hang forever if the daemon wedges mid-command */
  struct timeval tv = {15, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  return fd;
}

int xw_client_connect(XWError *err) {
  char sock[256];
  xw_daemon_socket_path(sock, sizeof(sock));

  int fd = connect_to(sock);
  if (fd >= 0)
    return fd;

  XW_ERROR_SET(err, 1, "daemon not running, start xwallpaperd first (%.100s): %s",
               sock, strerror(errno));
  return -1;
}

int xw_client_talk(int fd, const char *msg, size_t len, char *reply, size_t rn, XWError *err) {
  ssize_t w = write(fd, msg, len);
  if (w < 0 || (size_t)w != len) {
    XW_ERROR_SET(err, 1, "write to daemon failed: %s", strerror(errno));
    return -1;
  }

  size_t got = 0;
  while (got < rn - 1) {
    ssize_t r = read(fd, reply + got, rn - 1 - got);
    if (r < 0) {
      if (errno == EINTR)
        continue;
      XW_ERROR_SET(err, 1, "read from daemon failed: %s", strerror(errno));
      return -1;
    }
    if (r == 0)
      break; /* daemon closed the connection: full reply received */
    got += (size_t)r;
  }
  reply[got] = '\0';

  if (strncmp(reply, "ok", 2) == 0 && (reply[2] == '\n' || reply[2] == '\0'))
    return 0;
  if (strncmp(reply, "error", 5) == 0) {
    const char *m = reply + 5;
    if (*m == '\0' || *m == '\n')
      m = "unknown daemon error";
    XW_ERROR_SET(err, 1, "%s", m);
    return 1;
  }
  /* list / ok with payload: caller interprets */
  return 0;
}
