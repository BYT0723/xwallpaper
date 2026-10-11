#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "web_dir.h"

int xw_web_is_http_url(const char *path) {
  if (!path)
    return 0;
  return strncmp(path, "http://", 7) == 0 || strncmp(path, "https://", 8) == 0;
}

int xw_web_resolve(const char *in, char *root, size_t rn, char *entry, size_t en) {
  if (!in || !*in || !root || !entry || rn == 0 || en == 0)
    return -1;
  if (xw_web_is_http_url(in))
    return 1;
  struct stat st;
  if (stat(in, &st) != 0 || !S_ISDIR(st.st_mode))
    return -1;
  char *abs = realpath(in, NULL);
  if (!abs)
    return -1;
  char idx[4096];
  if (snprintf(idx, sizeof(idx), "%s/index.html", abs) >= (int)sizeof(idx)) {
    free(abs);
    return -1;
  }
  struct stat ist;
  int ok = stat(idx, &ist) == 0 && S_ISREG(ist.st_mode);
  if (!ok) {
    free(abs);
    return -1;
  }
  if (strlen(abs) + 1 > rn) {
    free(abs);
    return -1;
  }
  snprintf(root, rn, "%s", abs);
  free(abs);
  if (strlen("index.html") + 1 > en)
    return -1;
  snprintf(entry, en, "index.html");
  return 0;
}

const char *xw_web_mime_for(const char *filename) {
  static const struct {
    const char *ext, *mime;
  } table[] = {
    {"html", "text/html"}, {"htm", "text/html"},
    {"css", "text/css"},
    {"js", "text/javascript"}, {"mjs", "text/javascript"},
    {"json", "application/json"},
    {"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"},
    {"gif", "image/gif"}, {"svg", "image/svg+xml"}, {"webp", "image/webp"},
    {"ico", "image/x-icon"},
    {"woff", "font/woff"}, {"woff2", "font/woff2"},
    {"ttf", "font/ttf"}, {"otf", "font/otf"},
    {"mp4", "video/mp4"}, {"webm", "video/webm"},
    {"wasm", "application/wasm"},
    {"txt", "text/plain"}, {"xml", "application/xml"},
  };
  if (!filename)
    return "application/octet-stream";
  const char *dot = strrchr(filename, '.');
  if (!dot || dot[1] == '\0')
    return "application/octet-stream";
  char ext[16];
  size_t len = 0;
  for (const char *p = dot + 1; *p && len < sizeof(ext) - 1; p++)
    ext[len++] = (char)tolower((unsigned char)*p);
  ext[len] = '\0';
  for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
    if (strcmp(ext, table[i].ext) == 0)
      return table[i].mime;
  return "application/octet-stream";
}

static int hex_val(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

int xw_web_req_to_rel(const char *req_path, char *rel, size_t n) {
  if (!req_path || !rel || n == 0)
    return -1;
  while (*req_path == '/')
    req_path++;
  char dec[4096];
  size_t o = 0;
  for (const char *p = req_path; *p && o + 1 < sizeof(dec); p++) {
    if (*p == '%' && p[1] && p[2]) {
      int hi = hex_val(p[1]), lo = hex_val(p[2]);
      if (hi < 0 || lo < 0)
        return -1;
      dec[o++] = (char)((hi << 4) | lo);
      p += 2;
    } else if (*p == '\\') {
      return -1;
    } else {
      dec[o++] = *p;
    }
  }
  dec[o] = '\0';
  if (dec[0] == '\0') {
    if (strlen("index.html") + 1 > n)
      return -1;
    snprintf(rel, n, "index.html");
    return 0;
  }
  size_t dl = strlen(dec);
  if (dl > 0 && dec[dl - 1] == '/') {
    if (dl + strlen("index.html") + 1 > n)
      return -1;
    snprintf(rel, n, "%sindex.html", dec);
  } else {
    if (dl + 1 > n)
      return -1;
    snprintf(rel, n, "%s", dec);
  }
  /* reject any ".." segment */
  const char *s = rel;
  while (*s) {
    while (*s == '/')
      s++;
    if (!*s)
      break;
    const char *e = strchr(s, '/');
    size_t sl = e ? (size_t)(e - s) : strlen(s);
    if (sl == 2 && s[0] == '.' && s[1] == '.')
      return -1;
    if (!e)
      break;
    s = e;
  }
  return 0;
}
