#include <netlink/socket.h>
#include <netlink/genl/mngt.h>

#include <signal.h>
#include <poll.h>

#include "netlinkcom.h"

static void handleSIGINT(int sig);
static volatile int run = 1;

int main(int argc, char **argv) {
    int err;
    //nl_debug = 4;
    signal(SIGINT, handleSIGINT);

    struct nl_sock *sock = com_init();

    struct pollfd pfd = {
        .fd = nl_socket_get_fd(sock),
        .events = POLLIN,
    };

    while(run) {
        if(poll(&pfd, 1, 5000) > 0) {
            err = nl_recvmsgs_default(sock);
            if(err) {
                printf("recv error: %s\n", nl_geterror(err));
                break;
            }
        }
    }

    com_cleanup(sock);

    return 0;
}

static void handleSIGINT(int sig) {
    run = 0;    
}
