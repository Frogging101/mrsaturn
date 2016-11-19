#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <linux/if_alg.h>

#ifndef AF_ALG
  #define AF_ALG 38
#endif
#ifndef SOL_ALG
  #define SOL_ALG 279
#endif

#define BUF_LEN 2048

uint32_t alg_csumData(int sock, char *data, size_t len) {
    char *buf = data;

    while(len > 0) {
        int cnt = BUF_LEN;
        if(cnt > len)
            cnt = len;

        int flags = (len > cnt) ? MSG_MORE : 0;

        if(send(sock, buf, cnt, flags) != cnt) {
            printf("alg send() error: %s\n", strerror(errno));
            return 0;
        }

        buf += cnt;
        len -= cnt;
    }

    uint32_t csum = 0;
    errno = 0;
    if(recv(sock, &csum, 4, 0) != 4) {
        if(errno)
            printf("checksum recv() error: %s\n", strerror(errno));
        else
            printf("Received wrong number of bytes for checksum; unknown error\n");
        return 0;
    }

    return csum;
}

int alg_getsock() {
    struct sockaddr_alg crc32c_sa = {
        .salg_family = AF_ALG,
        .salg_type = "hash",
        .salg_name = "crc32c"
    };

    int sock = socket(AF_ALG, SOCK_SEQPACKET, 0);

    if(bind(sock, (struct sockaddr *) &crc32c_sa, sizeof(crc32c_sa)) != 0) {
        printf("Error binding alg socket: %s\n", strerror(errno));
        return -1;
    } /*else {
        printf("Socket bound successfully\n");
    }*/

    /*
     * For some reason, we need to set the key. The ~0 value is a default
     * found in crypto/crc32c_generic.c in the kernel source. Otherwise future
     * operations will fail with ENOKEY. This may be a bug.
     *
     * The documentation says that this is to be done on the fd returned by
     * accept(), but that will fail with EOPNOTSUPP.
     * TODO: Submit a snippet to the mailing list regarding these issues
     */

    uint32_t key = ~0;
    if(setsockopt(sock, SOL_ALG, ALG_SET_KEY, &key, sizeof(key)) == -1) {
        printf("Could not set key: %s\n", strerror(errno));
        return -1;
    }

    int ret = accept(sock, NULL, 0);
    if(ret < 0) {
        printf("alg accept() failed: %s\n", strerror(errno));
        return -1;
    }

    return ret;
}
