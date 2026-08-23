#ifndef XW_CLIENT_H
#define XW_CLIENT_H

#include "cli.h"
#include "xw_err.h"

/* Connect to the daemon socket. Returns fd, or -1 with err set.
 * If `spawn` is nonzero and no daemon is listening, fork one and wait for
 * the socket to appear before retrying the connection. */
int xw_client_connect(int spawn, XWError *err);

/* Send a command message (n bytes, NUL-separated fields) and read the reply.
 * Returns 0 on ok, 1 on daemon error reply (msg in err), -1 on io error.
 * The whole reply is left in reply (NUL-separated fields, may span lines). */
int xw_client_talk(int fd, const char *msg, size_t n, char *reply, size_t rn, XWError *err);

/* Send the exit command and close. */
int xw_client_exit(int fd, XWError *err);

/* Spawn a daemon process (fork + setsid). Parent returns immediately. */
int xw_client_spawn_daemon(XWError *err);

#endif /* XW_CLIENT_H */
