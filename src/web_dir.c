#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
