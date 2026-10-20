#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>
#include <json-c/json.h>

#include "props_store.h"

/* Sidecar dir: $XDG_STATE_HOME/xwallpaper/props (fallback
 * $HOME/.local/state/xwallpaper/props). Shared across platforms: the same
 * wallpaper root gets the same customization on X11 and Wayland. */
static int store_dir(char *out, size_t n) {
  const char *xdg = getenv("XDG_STATE_HOME");
  int r;
  if (xdg && xdg[0])
    r = snprintf(out, n, "%s/xwallpaper/props", xdg);
  else {
    const char *home = getenv("HOME");
    if (!home || !home[0])
      home = ".";
    r = snprintf(out, n, "%s/.local/state/xwallpaper/props", home);
  }
  return (r < 0 || r >= (int)n) ? -1 : 0;
}

int xw_props_store_path(const char *wallpaper_id, char *out, size_t n) {
  char dir[1024], *sum;
  if (!wallpaper_id || !wallpaper_id[0] || !out || n == 0)
    return -1;
  if (store_dir(dir, sizeof(dir)) != 0)
    return -1;
  sum = g_compute_checksum_for_string(G_CHECKSUM_SHA256, wallpaper_id, -1);
  if (!sum)
    return -1;
  {
    int r = snprintf(out, n, "%s/%s.json", dir, sum);
    g_free(sum);
    return (r < 0 || r >= (int)n) ? -1 : 0;
  }
}

static int ensure_dir_for(const char *path) {
  char dir[1100];
  snprintf(dir, sizeof(dir), "%s", path);
  {
    char *slash = strrchr(dir, '/');
    if (!slash)
      return 0;
    *slash = '\0';
  }
  g_mkdir_with_parents(dir, 0700);
  return 0;
}

int xw_props_store_save(const char *wallpaper_id,
                        const XWStoredProp *props, int n) {
  char path[1100], tmp[1200];
  FILE *f;
  struct json_object *root, *obj;
  const char *text;
  size_t len;
  int i;

  if (!wallpaper_id || !wallpaper_id[0])
    return -1;
  if (xw_props_store_path(wallpaper_id, path, sizeof(path)) != 0)
    return -1;
  if (n <= 0 || !props) {
    unlink(path);
    return 0;
  }
  if (n > XW_PROPS_STORE_MAX)
    return -1;

  root = json_object_new_object();
  obj = json_object_new_object();
  if (!root || !obj) {
    if (root)
      json_object_put(root);
    if (obj)
      json_object_put(obj);
    return -1;
  }
  json_object_object_add(root, "v", json_object_new_int(1));
  json_object_object_add(root, "id", json_object_new_string(wallpaper_id));
  for (i = 0; i < n; i++) {
    struct json_object *e;
    if (!props[i].name[0])
      continue;
    e = json_object_new_object();
    if (!e)
      continue;
    json_object_object_add(e, "type",
                           json_object_new_string(props[i].type));
    json_object_object_add(e, "value",
                           json_object_new_string(props[i].value));
    json_object_object_add(obj, props[i].name, e);
  }
  json_object_object_add(root, "props", obj);
  text = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
  if (!text) {
    json_object_put(root);
    return -1;
  }
  len = strlen(text);
  ensure_dir_for(path);
  snprintf(tmp, sizeof(tmp), "%s.tmp.%d", path, (int)getpid());
  f = fopen(tmp, "w");
  if (!f) {
    json_object_put(root);
    return -1;
  }
  chmod(tmp, 0600);
  if (fwrite(text, 1, len, f) != len || fwrite("\n", 1, 1, f) != 1) {
    fclose(f);
    unlink(tmp);
    json_object_put(root);
    return -1;
  }
  if (fclose(f) != 0) {
    unlink(tmp);
    json_object_put(root);
    return -1;
  }
  chmod(tmp, 0600);
  if (rename(tmp, path) != 0) {
    unlink(tmp);
    json_object_put(root);
    return -1;
  }
  json_object_put(root);
  return 0;
}

int xw_props_store_load(const char *wallpaper_id,
                        XWStoredProp *out, int cap, int *n) {
  char path[1100], *buf = NULL;
  FILE *f;
  long sz;
  size_t nr;
  struct json_object *root = NULL, *obj = NULL;
  int count = 0;

  if (n)
    *n = 0;
  if (!wallpaper_id || !wallpaper_id[0] || !out || cap <= 0 || !n)
    return -1;
  if (xw_props_store_path(wallpaper_id, path, sizeof(path)) != 0)
    return -1;
  f = fopen(path, "rb");
  if (!f)
    return errno == ENOENT ? 1 : -1;
  fseek(f, 0, SEEK_END);
  sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0 || sz > (1 << 20)) {
    fclose(f);
    return -1;
  }
  buf = malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return -1;
  }
  nr = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  if (nr != (size_t)sz) {
    free(buf);
    return -1;
  }
  buf[nr] = '\0';
  root = json_tokener_parse(buf);
  free(buf);
  if (!root)
    return -1;
  if (!json_object_object_get_ex(root, "props", &obj) || !obj ||
      json_object_get_type(obj) != json_type_object) {
    json_object_put(root);
    return -1;
  }
  json_object_object_foreach(obj, key, val) {
    struct json_object *t = NULL, *v = NULL;
    const char *ts = "", *vs = "";
    if (count >= cap) {
      json_object_put(root);
      return -1;
    }
    if (json_object_get_type(val) == json_type_object) {
      if (json_object_object_get_ex(val, "type", &t) && t &&
          json_object_get_type(t) == json_type_string) {
        const char *s = json_object_get_string(t);
        if (s)
          ts = s;
      }
      if (json_object_object_get_ex(val, "value", &v) && v) {
        const char *s = json_object_get_string(v);
        if (s)
          vs = s;
      }
    }
    snprintf(out[count].name, sizeof(out[count].name), "%s", key);
    snprintf(out[count].type, sizeof(out[count].type), "%s", ts);
    snprintf(out[count].value, sizeof(out[count].value), "%s", vs);
    count++;
  }
  json_object_put(root);
  *n = count;
  return 0;
}
