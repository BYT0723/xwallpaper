#ifndef XW_CLIENT_H
#define XW_CLIENT_H

#include "cli.h"
#include "xw_err.h"

/* Connect to the daemon socket. Returns fd, or -1 with err set.
 * Does not spawn a daemon — the daemon must be started separately
 * (xwallpaperd). On failure err->msg explains how to start it. */
int xw_client_connect(XWError *err);

/* Send a command message (n bytes, NUL-separated fields) and read the reply.
 * Returns 0 on ok, 1 on daemon error reply (msg in err), -1 on io error.
 * The whole reply is left in reply (NUL-separated fields, may span lines). */
int xw_client_talk(int fd, const char *msg, size_t n, char *reply, size_t rn, XWError *err);

#endif /* XW_CLIENT_H */
