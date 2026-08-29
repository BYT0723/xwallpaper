#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "harness.h"
#include "../src/daemon.h"
#include "../src/client.h"
#include "../src/proto.h"

static XWConfig cfg;

/* search for a substring within any of the NUL-separated fields of a message */
static int has_field(const char *msg, size_t n, const char *needle) {
  size_t nl = strlen(needle);
  size_t off = 0;
  while (off < n) {
    const char *end = memchr(msg + off, '\0', n - off);
    if (!end)
      break;
    size_t len = (size_t)(end - (msg + off));
    if (len >= nl && memmem(msg + off, len, needle, nl) != NULL)
      return 1;
    off = (size_t)(end + 1 - msg);
  }
  return 0;
}

static void reset(void) {
  memset(&cfg, 0, sizeof(cfg));
  cfg.backend_explicit = -1;
  cfg.scale_mode = XW_SCALE_CONTAIN;
  cfg.volume = 100.0;
  cfg.opacity = 1.0;
  cfg.target = XW_TARGET_PRIMARY;
}

static void test_encode_set_basic(void) {
  TEST_BEGIN("set encodes target/type/path with defaults");
  reset();
  cfg.command = XW_CMD_SET;
  cfg.target = XW_TARGET_PRIMARY;
  snprintf(cfg.path, sizeof(cfg.path), "wall.png");

  char msg[XW_MSG_MAX];
  int n = xw_proto_encode_set(&cfg, msg, sizeof(msg));
  TEST_CHECK(n > 0);
  TEST_CHECK(strncmp(msg, "set\0", 4) == 0);
  TEST_CHECK(has_field(msg, n, "wall.png"));
  TEST_CHECK(has_field(msg, n, "scale=contain"));
  TEST_CHECK(has_field(msg, n, "volume=100"));
}

static void test_encode_set_output(void) {
  TEST_BEGIN("set -m DP-1 encodes output name");
  reset();
  cfg.command = XW_CMD_SET;
  cfg.target = XW_TARGET_OUTPUT;
  snprintf(cfg.output, sizeof(cfg.output), "DP-1");
  snprintf(cfg.path, sizeof(cfg.path), "clip.mp4");
  cfg.backend_explicit = XW_BACKEND_VIDEO;
  cfg.mute = 1;
  cfg.volume = 40.0;

  char msg[XW_MSG_MAX];
  int n = xw_proto_encode_set(&cfg, msg, sizeof(msg));
  TEST_CHECK(has_field(msg, n, "DP-1"));
  TEST_CHECK(has_field(msg, n, "video"));
  TEST_CHECK(has_field(msg, n, "mute=1"));
  TEST_CHECK(has_field(msg, n, "volume=40"));
}

static void test_encode_set_video_fps(void) {
  TEST_BEGIN("set --fps encodes fps field; default omits it");
  reset();
  cfg.command = XW_CMD_SET;
  cfg.target = XW_TARGET_PRIMARY;
  snprintf(cfg.path, sizeof(cfg.path), "clip.mp4");
  cfg.backend_explicit = XW_BACKEND_VIDEO;
  cfg.fps = 30.0;

  char msg[XW_MSG_MAX];
  int n = xw_proto_encode_set(&cfg, msg, sizeof(msg));
  TEST_CHECK(n > 0);
  TEST_CHECK(has_field(msg, n, "fps=30"));

  /* default fps=0 is not encoded */
  reset();
  cfg.command = XW_CMD_SET;
  cfg.target = XW_TARGET_PRIMARY;
  snprintf(cfg.path, sizeof(cfg.path), "clip.mp4");
  cfg.backend_explicit = XW_BACKEND_VIDEO;
  n = xw_proto_encode_set(&cfg, msg, sizeof(msg));
  TEST_CHECK(n > 0);
  TEST_CHECK(!has_field(msg, n, "fps="));
}


static void test_encode_set_geom(void) {
  TEST_BEGIN("set -g encodes geometry");
  reset();
  cfg.command = XW_CMD_SET;
  cfg.target = XW_TARGET_GEOM;
  cfg.have_geom = 1;
  cfg.geom_x = 100;
  cfg.geom_y = 200;
  cfg.geom_w = 1920;
  cfg.geom_h = 1080;
  snprintf(cfg.path, sizeof(cfg.path), "a.png");

  char msg[XW_MSG_MAX];
  int n = xw_proto_encode_set(&cfg, msg, sizeof(msg));
  TEST_CHECK(has_field(msg, n, "geom"));
  TEST_CHECK(has_field(msg, n, "1920x1080+100+200"));
}

static void test_encode_clear(void) {
  TEST_BEGIN("clear encodes target (all vs output)");
  reset();
  cfg.command = XW_CMD_CLEAR;
  cfg.target = XW_TARGET_ALL;
  char msg[XW_MSG_MAX];
  xw_proto_encode_clear(&cfg, msg, sizeof(msg));
  TEST_CHECK(strcmp(msg, "clear\0all\n") == 0);

  cfg.target = XW_TARGET_OUTPUT;
  snprintf(cfg.output, sizeof(cfg.output), "HDMI-1");
  xw_proto_encode_clear(&cfg, msg, sizeof(msg));
  TEST_CHECK(strncmp(msg, "clear\0HDMI-1\n", 13) == 0);

  /* --keep appends keep=1 field */
  cfg.keep = 1;
  int nk = xw_proto_encode_clear(&cfg, msg, sizeof(msg));
  TEST_CHECK(strncmp(msg, "clear\0", 6) == 0);
  TEST_CHECK(has_field(msg, (size_t)nk, "keep=1"));
}

static void test_encode_restore(void) {
  TEST_BEGIN("restore encodes as verb-only line");
  char msg[64];
  TEST_CHECK_EQ_INT(xw_proto_encode_restore(msg, sizeof(msg)), 8);
  TEST_CHECK(strcmp(msg, "restore\n") == 0);
}

static void test_encode_list_exit(void) {
  TEST_BEGIN("list/exit encode as verb-only lines");
  char msg[64];
  TEST_CHECK_EQ_INT(xw_proto_encode_list(msg, sizeof(msg)), 5);
  TEST_CHECK(strcmp(msg, "list\n") == 0);
  TEST_CHECK_EQ_INT(xw_proto_encode_exit(msg, sizeof(msg)), 5);
  TEST_CHECK(strcmp(msg, "exit\n") == 0);
}

/* fake daemon in a child process: binds the real socket path, reads one
 * command, replies ok, and writes the received command to a temp file.
 * Parent runs the client against the child's listener, then reads the file. */
static int fake_daemon(char *got_cmd, size_t got_n) {
  setenv("XDG_RUNTIME_DIR", "/tmp", 1);
  char path[256];
  xw_daemon_socket_path(path, sizeof(path));
  char dir[256];
  xw_daemon_socket_path(dir, sizeof(dir));
  char *slash = strrchr(dir, '/');
  if (slash)
    *slash = '\0';
  mkdir(dir, 0700);
  unlink(path);
  unlink("/tmp/xw_roundtrip_got");

  int srv = socket(AF_UNIX, SOCK_STREAM, 0);
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path), "%.107s", path);
  if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    return -1;
  listen(srv, 1);

  pid_t pid = fork();
  if (pid < 0)
    return -1;
  if (pid == 0) {
    int c = accept(srv, NULL, NULL);
    if (c < 0)
      _exit(1);
    ssize_t r = read(c, got_cmd, got_n - 1);
    if (r < 0)
      r = 0;
    FILE *f = fopen("/tmp/xw_roundtrip_got", "wb");
    if (f) {
      fwrite(got_cmd, 1, (size_t)r, f);
      fclose(f);
    }
    write(c, "ok\n", 3);
    close(c);
    close(srv);
    unlink(path);
    _exit(0);
  }
  close(srv);
  /* parent: run the client against the child's listener */
  XWError err;
  memset(&err, 0, sizeof(err));
  int fd = xw_client_connect(&err);
  if (fd < 0) {
    TEST_FAIL("client connect failed: %s", err.msg);
  } else {
    char reply[512];
    reset();
    cfg.command = XW_CMD_SET;
    cfg.target = XW_TARGET_OUTPUT;
    snprintf(cfg.output, sizeof(cfg.output), "DP-1");
    snprintf(cfg.path, sizeof(cfg.path), "x png with spaces.png");
    cfg.backend_explicit = XW_BACKEND_IMAGE;
    char msg[XW_MSG_MAX];
    int n = xw_proto_encode_set(&cfg, msg, sizeof(msg));
    int r = xw_client_talk(fd, msg, (size_t)n, reply, sizeof(reply), &err);
    TEST_CHECK_EQ_INT(r, 0);
    close(fd);
  }
  int status;
  waitpid(pid, &status, 0);
  FILE *f = fopen("/tmp/xw_roundtrip_got", "rb");
  if (!f) {
    got_cmd[0] = '\0';
    return 0;
  }
  size_t got = fread(got_cmd, 1, got_n - 1, f);
  fclose(f);
  got_cmd[got] = '\0';
  return 0;
}

static void test_client_roundtrip(void) {
  TEST_BEGIN("client set -> real socket -> fake daemon -> ok");
  char got[XW_MSG_MAX];
  if (fake_daemon(got, sizeof(got)) != 0) {
    TEST_FAIL("cannot set up fake daemon");
    return;
  }
  /* verify the command that reached the daemon */
  reset();
  cfg.command = XW_CMD_SET;
  cfg.target = XW_TARGET_OUTPUT;
  snprintf(cfg.output, sizeof(cfg.output), "DP-1");
  snprintf(cfg.path, sizeof(cfg.path), "x png with spaces.png");
  cfg.backend_explicit = XW_BACKEND_IMAGE;
  char expected[XW_MSG_MAX];
  int n = xw_proto_encode_set(&cfg, expected, sizeof(expected));
  TEST_CHECK_EQ_INT(memcmp(got, expected, (size_t)n), 0);
}

static void test_encode_set_video_extras(void) {
  TEST_BEGIN("set encodes rotate and keybinds");
  reset();
  cfg.command = XW_CMD_SET;
  cfg.target = XW_TARGET_OUTPUT;
  snprintf(cfg.output, sizeof(cfg.output), "DP-1");
  snprintf(cfg.path, sizeof(cfg.path), "clip.mp4");
  cfg.backend_explicit = XW_BACKEND_VIDEO;
  cfg.rotate = 90;
  snprintf(cfg.keybinds, sizeof(cfg.keybinds), "/tmp/keys.conf");

  char msg[XW_MSG_MAX];
  int n = xw_proto_encode_set(&cfg, msg, sizeof(msg));
  TEST_CHECK(has_field(msg, n, "rotate=90"));
  TEST_CHECK(has_field(msg, n, "keybinds=/tmp/keys.conf"));
}

static void test_encode_set_rotate_default_omitted(void) {
  TEST_BEGIN("default rotate (0) is omitted from the wire");
  reset();
  cfg.command = XW_CMD_SET;
  cfg.target = XW_TARGET_PRIMARY;
  snprintf(cfg.path, sizeof(cfg.path), "a.mp4");
  cfg.backend_explicit = XW_BACKEND_VIDEO;

  char msg[XW_MSG_MAX];
  int n = xw_proto_encode_set(&cfg, msg, sizeof(msg));
  TEST_CHECK(!has_field(msg, n, "rotate="));
  TEST_CHECK(!has_field(msg, n, "keybinds="));
}

static void test_encode_set_audio_client_name(void) {
  TEST_BEGIN("set encodes audio_client_name; default omitted");
  reset();
  cfg.command = XW_CMD_SET;
  cfg.target = XW_TARGET_PRIMARY;
  snprintf(cfg.path, sizeof(cfg.path), "clip.mp4");
  cfg.backend_explicit = XW_BACKEND_VIDEO;
  snprintf(cfg.audio_client_name, sizeof(cfg.audio_client_name), "xwallpaper");

  char msg[XW_MSG_MAX];
  int n = xw_proto_encode_set(&cfg, msg, sizeof(msg));
  TEST_CHECK(n > 0);
  TEST_CHECK(has_field(msg, n, "audio_client_name=xwallpaper"));

  /* custom name round-trips */
  snprintf(cfg.audio_client_name, sizeof(cfg.audio_client_name), "mywall");
  n = xw_proto_encode_set(&cfg, msg, sizeof(msg));
  TEST_CHECK(has_field(msg, n, "audio_client_name=mywall"));

  /* empty default is not encoded */
  cfg.audio_client_name[0] = '\0';
  n = xw_proto_encode_set(&cfg, msg, sizeof(msg));
  TEST_CHECK(n > 0);
  TEST_CHECK(!has_field(msg, n, "audio_client_name="));
}

int main(void) {
  TEST_SUITE("client/protocol");
  test_encode_set_basic();
  test_encode_set_output();
  test_encode_set_geom();
  test_encode_clear();
  test_encode_restore();
  test_encode_list_exit();
  test_encode_set_video_extras();
  test_encode_set_rotate_default_omitted();
  test_encode_set_video_fps();
  test_encode_set_audio_client_name();
  test_client_roundtrip();
  TEST_SUMMARY();
}
