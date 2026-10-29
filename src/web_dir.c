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

/* Read the top-level "file" string from <dir>/project.json (Wallpaper Engine
 * entry file, e.g. "index.html" or "wallpaper.html"). Copies the value to
 * out and returns 1 on success, 0 otherwise.
 *
 * Dependency-free on purpose: web_dir stays thin (json-c is optional
 * project-wide, see HAVE_JSONC). Only a top-level string key exactly equal
 * to "file" counts; nested keys and non-string values are ignored. Any
 * anomaly (missing/corrupt file, no key, bad escapes) returns 0 and the
 * caller falls back to index.html, so a parser miss can only ever restore
 * the old behavior, never pick a wrong file. */
static int project_entry_file(const char *dir, char *out, size_t on) {
  char pj[4096];
  FILE *f;
  long sz;
  char *buf;
  size_t i = 0, n;
  int depth = 0, in_str = 0, esc = 0;
  size_t ks = 0; /* start of current string body */
  int is_key = 0;

  if (!dir || !out || on == 0)
    return 0;
  if (snprintf(pj, sizeof(pj), "%s/project.json", dir) >= (int)sizeof(pj))
    return 0;
  f = fopen(pj, "rb");
  if (!f)
    return 0;
  fseek(f, 0, SEEK_END);
  sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0 || sz > (1 << 20)) {
    fclose(f);
    return 0;
  }
  buf = malloc((size_t)sz);
  if (!buf) {
    fclose(f);
    return 0;
  }
  n = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  if (n != (size_t)sz) {
    free(buf);
    return 0;
  }

  while (i < n) {
    char c = buf[i];
    if (in_str) {
      if (esc) {
        esc = 0;
      } else if (c == '\\') {
        esc = 1;
      } else if (c == '"') {
        in_str = 0;
        if (is_key) {
          /* a top-level key just closed: check name, then expect a value */
          size_t klen = i - ks;
          size_t j = i + 1;
          is_key = 0;
          if (depth == 1 && klen == 4 && memcmp(buf + ks, "file", 4) == 0) {
            size_t v = 0;
            while (j < n && (buf[j] == ' ' || buf[j] == '\t' ||
                             buf[j] == '\n' || buf[j] == '\r'))
              j++;
            if (j < n && buf[j] == ':') {
              j++;
              while (j < n && (buf[j] == ' ' || buf[j] == '\t' ||
                               buf[j] == '\n' || buf[j] == '\r'))
                j++;
              if (j < n && buf[j] == '"') {
                j++;
                while (j < n) {
                  char d = buf[j];
                  if (d == '\\') {
                    /* only plain-char escapes can occur in a safe
                     * filename; anything else fails closed */
                    if (j + 1 >= n)
                      break;
                    char e = buf[j + 1];
                    if (e != '"' && e != '\\' && e != '/')
                      break;
                    if (v + 1 >= on)
                      break;
                    out[v++] = e;
                    j += 2;
                    continue;
                  }
                  if (d == '"') {
                    out[v] = '\0';
                    free(buf);
                    return v > 0 ? 1 : 0;
                  }
                  if ((unsigned char)d < 0x20 || (unsigned char)d == 0x7f)
                    break;
                  if (v + 1 >= on)
                    break;
                  out[v++] = d;
                  j++;
                }
                free(buf);
                return 0;
              }
            }
          }
        }
      }
      i++;
      continue;
    }
    if (c == '"') {
      in_str = 1;
      ks = i + 1;
      /* a string is a key candidate only right inside an object at the
       * top level; the ':' check after close decides */
      is_key = (depth == 1);
    } else if (c == '{' || c == '[') {
      depth++;
      is_key = 0;
    } else if (c == '}' || c == ']') {
      depth--;
      is_key = 0;
    }
    i++;
  }
  free(buf);
  return 0;
}

/* A project.json "file" value is safe only as a bare filename: no path
 * separators, no parent refs, non-empty. The caller stats it under root. */
static int entry_name_ok(const char *name) {
  size_t k;
  if (!name || !*name)
    return 0;
  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    return 0;
  for (k = 0; name[k]; k++) {
    if (name[k] == '/' || name[k] == '\\')
      return 0;
  }
  return 1;
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
  /* Wallpaper Engine entry: project.json "file" wins when it names an
   * existing file (e.g. "wallpaper.html"); otherwise index.html. */
  const char *entry_name = "index.html";
  char alt[256];
  if (project_entry_file(abs, alt, sizeof(alt)) && entry_name_ok(alt)) {
    char probe[4096];
    struct stat pst;
    if (snprintf(probe, sizeof(probe), "%s/%s", abs, alt) < (int)sizeof(probe) &&
        stat(probe, &pst) == 0 && S_ISREG(pst.st_mode))
      entry_name = alt;
  }
  char idx[4096];
  if (snprintf(idx, sizeof(idx), "%s/%s", abs, entry_name) >= (int)sizeof(idx)) {
    free(abs);
    return -1;
  }
  struct stat ist;
  if (stat(idx, &ist) != 0 || !S_ISREG(ist.st_mode)) {
    free(abs);
    return -1;
  }
  if (strlen(abs) + 1 > rn) {
    free(abs);
    return -1;
  }
  snprintf(root, rn, "%s", abs);
  free(abs);
  if (strlen(entry_name) + 1 > en)
    return -1;
  snprintf(entry, en, "%s", entry_name);
  return 0;
}
