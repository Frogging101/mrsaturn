#include <stdint.h>

#include <sys/sysmacros.h>

#include <netlink/socket.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/mngt.h>

#include "netlinkcom.h"
#include "md.h"

enum {
    BTRFS_CSMM_C_UNSPEC,
    BTRFS_CSMM_C_HELLO,
    BTRFS_CSMM_C_GOODBYE,
    BTRFS_CSMM_C_ECHO,
    BTRFS_CSMM_C_MISMATCH,
    BTRFS_CSMM_C_MISMATCH_PROCESSED,
    __BTRFS_CSMM_C_MAX,
};

enum {
    BTRFS_CSMM_A_UNSPEC,
    BTRFS_CSMM_A_MSG,
    BTRFS_CSMM_A_CSUM_ACTUAL,
    BTRFS_CSMM_A_CSUM_EXPECTED,
    BTRFS_CSMM_A_PHYS,
    BTRFS_CSMM_A_LENGTH,
    BTRFS_CSMM_A_MAJOR,
    BTRFS_CSMM_A_MINOR,
    BTRFS_CSMM_A_FIXED,
    __BTRFS_CSMM_A_MAX
};

static int com_handle_valid_msg(struct nl_msg *msg, void *arg);
static int com_process_echo(struct nl_cache_ops *, struct genl_cmd *, struct genl_info *, void *);
static int com_process_mismatch(struct nl_cache_ops *, struct genl_cmd *, struct genl_info *, void *);
static int com_sayHello(struct nl_sock *sock);
static int com_sayGoodbye(struct nl_sock *sock);

#define BTRFS_CSMM_C_MAX (__BTRFS_CSMM_C_MAX - 1)
#define BTRFS_CSMM_A_MAX (__BTRFS_CSMM_A_MAX - 1)

struct genl_cmd mycmds[] = {
    {
        .c_id = BTRFS_CSMM_C_ECHO,
        .c_name = "echo",
        .c_maxattr = BTRFS_CSMM_A_MAX,
        .c_msg_parser = com_process_echo,
    },
    {
        .c_id = BTRFS_CSMM_C_MISMATCH,
        .c_name = "mismatch",
        .c_maxattr = BTRFS_CSMM_A_MAX,
        .c_msg_parser = com_process_mismatch,
    },
};

struct genl_ops myfam = {
    .o_name = "BTRFS_CSMM",
    .o_hdrsize = 0,
    .o_cmds = mycmds,
    .o_ncmds = 2,
};

struct nl_sock* com_init() {
    int err;

    struct nl_sock *sock = nl_socket_alloc();
    nl_socket_disable_seq_check(sock);
    nl_socket_modify_cb(sock, NL_CB_VALID, NL_CB_CUSTOM, com_handle_valid_msg, sock);
    genl_connect(sock);

    err = genl_register_family(&myfam);
    if(err) {
        printf("Error registering family\n");
        return NULL;
    }

    err = genl_ops_resolve(sock, &myfam);
    if(err) {
        printf("Could not resolve family\n");
        //return NULL;
    }

    err = com_sayHello(sock);
    if(err) {
        printf("Registration with kernel failed\n");
        return NULL;
    } else {
        printf("Registered with kernel (Port ID %d)\n",
               nl_socket_get_local_port(sock));
    }
    return sock;
}

int com_cleanup(struct nl_sock *sock) {
    int err = com_sayGoodbye(sock);
    if(err) {
        printf("De-registration failed\n");
        return 1;
    } else {
        printf("Successfully unregistered\n");
    }

    nl_socket_free(sock);
    return 0;
}

static int com_sayHello(struct nl_sock *sock) {
    int err;
    struct nl_msg *hello = nlmsg_alloc();
    void *head = genlmsg_put(hello, NL_AUTO_PORT, NL_AUTO_SEQ, myfam.o_id, 0,
                             0, BTRFS_CSMM_C_HELLO, 1);
    if(!head) {
        printf("Failed to initialize message headers\n");
        return 1;
    }

    err = nl_send_sync(sock, hello);
    if(err) {
        printf("send error: %s\n", nl_geterror(err));
        return 1;
    }

    return 0;
}

static int com_sayGoodbye(struct nl_sock *sock) {
    int err;
    struct nl_msg *hello = nlmsg_alloc();
    void *head = genlmsg_put(hello, NL_AUTO_PORT, NL_AUTO_SEQ, myfam.o_id, 0,
                             0, BTRFS_CSMM_C_GOODBYE, 1);
    if(!head) {
        printf("Failed to initialize message headers\n");
        return 1;
    }

    err = nl_send_sync(sock, hello);
    if(err) {
        printf("send error: %s\n", nl_geterror(err));
        return 1;
    }

    return 0;
}

static int com_handle_valid_msg(struct nl_msg *msg, void *arg) {
    int err;
    err = genl_handle_msg(msg, arg);
    if(err) {
        printf("message handling error: %s\n", nl_geterror(344));
    }
    return NL_OK;
}

static int com_process_echo(struct nl_cache_ops *cops, struct genl_cmd *cmd,
                           struct genl_info *info, void *ptr) {
    char *msg = nla_get_string(info->attrs[BTRFS_CSMM_A_MSG]);
    printf("Echo: %s\n", msg);
    return 0;
}

static int com_process_mismatch(struct nl_cache_ops *cops, struct genl_cmd *cmd,
                               struct genl_info *info, void *ptr) {
    uint64_t phys = nla_get_u64(info->attrs[BTRFS_CSMM_A_PHYS]);
    uint32_t csum_actual = nla_get_u32(info->attrs[BTRFS_CSMM_A_CSUM_ACTUAL]);
    uint32_t csum_expected = nla_get_u32(info->attrs[BTRFS_CSMM_A_CSUM_EXPECTED]);
    unsigned int major = nla_get_u32(info->attrs[BTRFS_CSMM_A_MAJOR]);
    unsigned int minor = nla_get_u32(info->attrs[BTRFS_CSMM_A_MINOR]);
    size_t len = nla_get_u64(info->attrs[BTRFS_CSMM_A_LENGTH]);
    printf("-----Mismatch-----\n");
    printf("phys: %lu\n", phys);
    printf("csum_actual: %u\n", csum_actual);
    printf("csum_expected: %u\n", csum_expected);
    printf("dev: %u:%u\n", major, minor);

    struct mddev mddev;
    initmddev(makedev(major, minor), &mddev);
    int rc = mdrepair(&mddev, phys, len, csum_actual, csum_expected);

    /*struct stripe s;
    uint64_t lsector = phys/512;
    getStripe(lsector, &s);

    printf("Sector: %lu\n", s.sector);
    printf("dd_idx: %d\n", s.dd_idx);
    printf("pd_idx: %d\n", s.pd_idx);
    */

    struct nl_msg *response = nlmsg_alloc();
    void *head = genlmsg_put(response, NL_AUTO_PORT, NL_AUTO_SEQ, myfam.o_id, 0,
                             0, BTRFS_CSMM_C_MISMATCH_PROCESSED, 1);
    if(!head) {
        printf("Failed to initialize message headers\n");
        return 1;
    }

    if(rc == 0)
        nla_put_flag(response, BTRFS_CSMM_A_FIXED);

    int err = nl_send_sync((struct nl_sock *) ptr, response);
    if(err) {
        printf("send error: %s\n", nl_geterror(err));
        return 1;
    }

    return NL_OK;
}
