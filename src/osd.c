#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "osd.h"

int xw_osd_format(const char *name, const char *value, int cur, char *out,
                   size_t n) {
  if (!name || !name[0] || !out || n == 0)
    return -1;
  if (strcmp(name, "volume") == 0) {
    if (cur < 0)
      cur = 0;
    if (cur > 100)
      cur = 100;
    snprintf(out, n, "Volume: %d%%", cur);
    return 0;
  }
  if (strcmp(name, "mute") == 0) {
    snprintf(out, n, "Mute: %s", cur ? "on" : "off");
    return 0;
  }
  if (strcmp(name, "pause") == 0) {
    snprintf(out, n, "%s", cur ? "Paused" : "Playing");
    return 0;
  }
  if (strcmp(name, "seek") == 0) {
    char *end = NULL;
    if (!value || !value[0])
      return -1;
    strtod(value, &end);
    if (!end || end == value || *end != '\0')
      return -1;
    if (value[0] == '+' || value[0] == '-')
      snprintf(out, n, "Seek %ss", value);
    else
      snprintf(out, n, "Seek +%ss", value);
    return 0;
  }
  if (strcmp(name, "rotate") == 0) {
    snprintf(out, n, "Rotate: %d", cur);
    return 0;
  }
  if (strcmp(name, "reload") == 0) {
    snprintf(out, n, "Reloaded");
    return 0;
  }
  return -1;
}
