#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "web_dir_scan.h"

#define XW_DIR_SCAN_DEPTH 8
#define XW_DIR_SCAN_DEFAULT_MAX 4096

static const char *ext_of(const char *path) {
  const char *dot, *slash;
  if (!path)
    return NULL;
  dot = strrchr(path, '.');
  slash = strrchr(path, '/');
  if (!dot || (slash && dot < slash) || !dot[1])
    return NULL;
  return dot + 1;
}

int xw_web_dir_match(const char *path, const char *kind) {
  const char *ext = ext_of(path);
  if (!ext)
    return 0;
  if (kind && strcmp(kind, "video") == 0)
    return strcasecmp(ext, "webm") == 0 || strcasecmp(ext, "ogg") == 0 ||
           strcasecmp(ext, "ogv") == 0;
  if (kind && strcmp(kind, "all") == 0)
    return 1;
  return strcasecmp(ext, "png") == 0 || strcasecmp(ext, "pnga") == 0 ||
         strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0 ||
         strcasecmp(ext, "bmp") == 0 || strcasecmp(ext, "gif") == 0 ||
         strcasecmp(ext, "svg") == 0 || strcasecmp(ext, "webp") == 0;
}

static int scan_dir(const char *dir, const char *kind, char ***arr, int *n,
                    int *cap, int max, int depth) {
  DIR *d;
  struct dirent *e;
  if (depth > XW_DIR_SCAN_DEPTH)
    return 0;
  d = opendir(dir);
  if (!d)
    return 0;
  while ((e = readdir(d)) != NULL && *n < max) {
    char path[4096];
    struct stat st;
    if (e->d_name[0] == '.' &&
        (e->d_name[1] == '\0' ||
         (e->d_name[1] == '.' && e->d_name[2] == '\0')))
      continue;
    if (snprintf(path, sizeof(path), "%s/%s", dir, e->d_name) >=
        (int)sizeof(path))
      continue;
    if (lstat(path, &st) != 0 || S_ISLNK(st.st_mode))
      continue; /* never follow links: no escapes, no loops */
    if (S_ISDIR(st.st_mode)) {
      (void)scan_dir(path, kind, arr, n, cap, max, depth + 1);
      continue;
    }
    if (!S_ISREG(st.st_mode) || !xw_web_dir_match(path, kind))
      continue;
    if (*n == *cap) {
      int next = *cap ? *cap * 2 : 32;
      char **grown;
      if (next > max)
        next = max;
      grown = realloc(*arr, sizeof(**arr) * (size_t)next);
      if (!grown)
        break;
      *arr = grown;
      *cap = next;
    }
    (*arr)[*n] = strdup(path);
    if (!(*arr)[*n])
      break;
    (*n)++;
  }
  closedir(d);
  return 0;
}

static int cmp_path(const void *a, const void *b) {
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int xw_web_dir_scan(const char *dir, const char *kind, char ***out, int max) {
  char **arr = NULL;
  int n = 0, cap = 0;
  if (out)
    *out = NULL;
  if (!dir || !dir[0] || !out)
    return -1;
  if (max <= 0)
    max = XW_DIR_SCAN_DEFAULT_MAX;
  {
    DIR *probe = opendir(dir);
    if (!probe)
      return -1;
    closedir(probe);
  }
  if (scan_dir(dir, kind, &arr, &n, &cap, max, 0) != 0) {
    for (int i = 0; i < n; i++)
      free(arr[i]);
    free(arr);
    return -1;
  }
  if (n > 1)
    qsort(arr, (size_t)n, sizeof(*arr), cmp_path);
  *out = arr;
  return n;
}
