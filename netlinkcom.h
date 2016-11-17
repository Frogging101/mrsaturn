#ifndef NETLINKCOM_H
#define NETLINKCOM_H

#include <netlink/socket.h>

struct nl_sock* com_init();
int com_cleanup(struct nl_sock *sock);

#endif
