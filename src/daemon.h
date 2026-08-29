#ifndef XW_DAEMON_H
#define XW_DAEMON_H

/* Daemon entry point: setsid, unlink stale socket, run the socket server. */
int xw_daemon_main(void);

#endif /* XW_DAEMON_H */
