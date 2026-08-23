#ifndef XW_ERR_H
#define XW_ERR_H

typedef struct XWError {
  int code;       /* 0 = ok, non-zero = module-specific error */
  char msg[256];  /* human-readable message */
} XWError;

#define XW_ERR_OK 0

#define XW_ERROR_SET(e, c, ...)                \
  do {                                         \
    if ((e)) {                                 \
      (e)->code = (c);                         \
      snprintf((e)->msg, sizeof((e)->msg), __VA_ARGS__); \
    }                                          \
  } while (0)

#define XW_ERROR_OK(e) ((e) == NULL || (e)->code == 0)

#endif /* XW_ERR_H */
