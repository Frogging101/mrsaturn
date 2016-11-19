#include <signal.h>
#include <poll.h>
#include <unistd.h>

#include <netlink/socket.h>
#include <netlink/genl/mngt.h>

#include <libudev.h>

#include "netlinkcom.h"
#include "algsocket.h"

static void handleSIGINT(int sig);
struct udev *udevctx = NULL;
int algsocket = -1;
static volatile int run = 1;

int main(int argc, char **argv) {
    int err;
    //nl_debug = 4;
    signal(SIGINT, handleSIGINT);

    udevctx = udev_new();
    algsocket = alg_getsock();

    struct nl_sock *nlsock = com_init();

    struct pollfd pfd = {
        .fd = nl_socket_get_fd(nlsock),
        .events = POLLIN,
    };

    while(run) {
        if(poll(&pfd, 1, 5000) > 0) {
            err = nl_recvmsgs_default(nlsock);
            if(err) {
                printf("nl recv error: %s\n", nl_geterror(err));
                break;
            }
        }
    }

    com_cleanup(nlsock);
    udev_unref(udevctx);
    close(algsocket);

    return 0;
}

static void handleSIGINT(int sig) {
    run = 0;    
}
