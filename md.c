#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/sysmacros.h>
#include <sys/types.h>
#include <fcntl.h>

#include <libudev.h>

#include "algsocket.h"
#include "md.h"
#include "saturnd.h"

#define ALGORITHM_LEFT_ASYMMETRIC       0 /* Rotating Parity N with Data Restart */
#define ALGORITHM_RIGHT_ASYMMETRIC      1 /* Rotating Parity 0 with Data Restart */
#define ALGORITHM_LEFT_SYMMETRIC        2 /* Rotating Parity N with Data Continuation */
#define ALGORITHM_RIGHT_SYMMETRIC       3 /* Rotating Parity 0 with Data Continuation */

#define ALGORITHM_PARITY_0              4 /* P or P,Q are initial devices */
#define ALGORITHM_PARITY_N              5 /* P or P,Q are final devices. */

#define ALGORITHM_ROTATING_ZERO_RESTART 8 /* DDF PRL=6 RLQ=1 */
#define ALGORITHM_ROTATING_N_RESTART    9 /* DDF PRL=6 RLQ=2 */
#define ALGORITHM_ROTATING_N_CONTINUE   10 /*DDF PRL=6 RLQ=3 */

#define ALGORITHM_LEFT_ASYMMETRIC_6     16
#define ALGORITHM_RIGHT_ASYMMETRIC_6    17
#define ALGORITHM_LEFT_SYMMETRIC_6      18
#define ALGORITHM_RIGHT_SYMMETRIC_6     19
#define ALGORITHM_PARITY_0_6            20
#define ALGORITHM_PARITY_N_6            ALGORITHM_PARITY_N


/*
struct stripe stripe;

getStripe(lsector, &stripe);

unsigned int cs_expected; // Expected checksum
unsigned int cs_data;     // Checksum of data on disk
unsigned int cs_rdata;    // Checksum of reconstructed data
*/

struct stripe {
    uint64_t sector;
    int dd_idx; // Disk containing sector
    int pd_idx; // Parity disk P
    //int qd_idx; // Parity disk Q (raid6)
};

int initmddev(dev_t devnum, struct mddev *mddev) {
    if(mddev == NULL)
        return -1;

    char path[PATH_MAX];

    struct udev_device *ud = udev_device_new_from_devnum(udevctx, 'b', devnum);
    if(ud == NULL)
        return -1;
    const char *syspath = udev_device_get_syspath(ud);

    strncpy(path, syspath, PATH_MAX);
    strcpy(mddev->syspath, syspath);
    strcpy(mddev->devnode, udev_device_get_devnode(ud));
    udev_device_unref(ud);

    int pathend = strlen(path);

    // Get layout
    FILE *f = fopen(strncat(path,"/md/layout", PATH_MAX), "r");
    fscanf(f, "%d", &mddev->layout);
    fclose(f);
    path[pathend] = '\0';

    // Get level
    f = fopen(strncat(path, "/md/level", PATH_MAX), "r");
    fscanf(f, "raid%d", &mddev->level);
    fclose(f);
    path[pathend] = '\0';

    // Get number of RAID disks
    f = fopen(strncat(path, "/md/raid_disks", PATH_MAX), "r");
    fscanf(f, "%d", &mddev->raid_disks);
    fclose(f);
    path[pathend] = '\0';

    // Get chunk size
    f = fopen(strncat(path, "/md/chunk_size", PATH_MAX), "r");
    fscanf(f, "%d", &mddev->chunk_size);
    fclose(f);
    path[pathend] = '\0';

    mddev->disks = calloc(mddev->raid_disks, sizeof(char *));
    mddev->offsets = calloc(mddev->raid_disks, sizeof(uint64_t));

    char tmp[PATH_MAX];
    strcpy(tmp, path);
    for(int i=0; i<mddev->raid_disks; i++) {
        int major = -1;
        int minor = -1;

        snprintf(path, PATH_MAX, "%s/md/rd%d/block/dev", tmp, i);

        // Get major/minor number of raid disk
        f = fopen(path, "r");
        fscanf(f, "%d:%d", &major, &minor);
        fclose(f);
        path[pathend] = '\0';

        snprintf(path, PATH_MAX, "%s/md/rd%d/offset", tmp, i);
        f = fopen(path, "r");
        fscanf(f, "%lu", &mddev->offsets[i]);
        fclose(f);
        path[pathend] = '\0';

        // Get device node path from udev
        ud = udev_device_new_from_devnum(udevctx, 'b', makedev(major, minor));
        const char *diskpath = udev_device_get_devnode(ud);

        // Store in array
        mddev->disks[i] = malloc(strlen(diskpath));
        strcpy(mddev->disks[i], diskpath);
        udev_device_unref(ud);
    }

    return 0;
}

static int getStripe(struct mddev *mddev, uint64_t lsector, struct stripe *stripe) {
    if(stripe == NULL || mddev == NULL)
        return -1;
    //int qd_idx = -1;
    int pd_idx = -1, dd_idx = -1;

    int layout = mddev->layout;
    int level = mddev->level;
    int raid_disks = mddev->raid_disks;
    int max_degraded = level == 6 ? 2 : 1;
    int data_disks = raid_disks - max_degraded;

    // sector_div(a, b) makes a = a/b and returns a % b

    int sectors_per_chunk = mddev->chunk_size >> 9;
    // chunk_offset = sector_div(lsector, sectors_per_chunk)
    unsigned int chunk_offset = lsector % sectors_per_chunk;
    // chunk_number = lsector
    uint64_t chunk_number = lsector/sectors_per_chunk;

    // stripe_num = chunk_number
    // dd_idx = sector_div(stripe_num, data_disks)
    dd_idx = chunk_number % data_disks;
    int stripe_num = chunk_number/data_disks;

    // Select parity disk based on algorithm
    switch(level) {
    case 4:
        pd_idx = data_disks;
        break;
    case 5:
        switch(layout) {
        case ALGORITHM_LEFT_ASYMMETRIC:
            pd_idx = data_disks - stripe_num % raid_disks;
            if(dd_idx >= pd_idx)
                dd_idx++;
            break;
        case ALGORITHM_RIGHT_ASYMMETRIC:
            pd_idx = stripe_num % raid_disks;
            if (dd_idx >= pd_idx)
                    dd_idx++;
            break;
        case ALGORITHM_LEFT_SYMMETRIC:
            pd_idx = data_disks - stripe_num % raid_disks;
            dd_idx = (pd_idx + 1 + dd_idx) % raid_disks;
            break;
        case ALGORITHM_RIGHT_SYMMETRIC:
            pd_idx = stripe_num % raid_disks;
            dd_idx = (pd_idx + 1 + dd_idx) % raid_disks;
            break;
        case ALGORITHM_PARITY_0:
            pd_idx = 0;
            dd_idx++;
            break;
        case ALGORITHM_PARITY_N:
            pd_idx = data_disks;
            break;
        default:
            //BUG();
            break;
        }
        break;

    //case 6:
    //    switch (layout) {
    //    case ALGORITHM_LEFT_ASYMMETRIC:
    //        pd_idx = raid_disks - 1 - stripe_num % raid_disks;
    //        qd_idx = pd_idx + 1;
    //        if (pd_idx == raid_disks-1) {
    //                dd_idx++;    /* Q D D D P */
    //                qd_idx = 0;
    //        } else if (dd_idx >= pd_idx)
    //                dd_idx += 2; /* D D P Q D */
    //        break;
    //    case ALGORITHM_RIGHT_ASYMMETRIC:
    //        pd_idx = stripe_num % raid_disks;
    //        qd_idx = pd_idx + 1;
    //        if (pd_idx == raid_disks-1) {
    //                dd_idx++;    /* Q D D D P */
    //                qd_idx = 0;
    //        } else if (dd_idx >= pd_idx)
    //                dd_idx += 2; /* D D P Q D */
    //        break;
    //    case ALGORITHM_LEFT_SYMMETRIC:
    //        pd_idx = raid_disks - 1 - stripe_num % raid_disks;
    //        qd_idx = (pd_idx + 1) % raid_disks;
    //        dd_idx = (pd_idx + 2 + dd_idx) % raid_disks;
    //        break;
    //    case ALGORITHM_RIGHT_SYMMETRIC:
    //        pd_idx = stripe_num % raid_disks;
    //        qd_idx = (pd_idx + 1) % raid_disks;
    //        dd_idx = (pd_idx + 2 + dd_idx) % raid_disks;
    //        break;

    //    case ALGORITHM_PARITY_0:
    //        pd_idx = 0;
    //        qd_idx = 1;
    //        dd_idx += 2;
    //        break;
    //    case ALGORITHM_PARITY_N:
    //        pd_idx = data_disks;
    //        qd_idx = data_disks + 1;
    //        break;

    //    case ALGORITHM_ROTATING_ZERO_RESTART:
    //        /* Exactly the same as RIGHT_ASYMMETRIC, but or
    //         * of blocks for computing Q is different.
    //         */
    //        pd_idx = stripe_num % raid_disks;
    //        qd_idx = pd_idx + 1;
    //        if (pd_idx == raid_disks-1) {
    //                dd_idx++;    /* Q D D D P */
    //                qd_idx = 0;
    //        } else if (dd_idx >= pd_idx)
    //                dd_idx += 2; /* D D P Q D */
    //        ddf_layout = 1;
    //            break;

    //    case ALGORITHM_ROTATING_N_RESTART:
    //        /* Same a left_asymmetric, by first stripe is
    //         * D D D P Q  rather than
    //         * Q D D D P
    //         */
    //        stripe_num += 1;
    //        pd_idx = raid_disks - 1 - stripe_num % raid_disks;
    //        qd_idx = pd_idx + 1;
    //        if (pd_idx == raid_disks-1) {
    //                dd_idx++;    /* Q D D D P */
    //                qd_idx = 0;
    //        } else if (dd_idx >= pd_idx)
    //                dd_idx += 2; /* D D P Q D */
    //        ddf_layout = 1;
    //        break;

    //    case ALGORITHM_ROTATING_N_CONTINUE:
    //        /* Same as left_symmetric but Q is before P */
    //        pd_idx = raid_disks - 1 - stripe_num % raid_disks;
    //        qd_idx = (pd_idx + raid_disks - 1) % raid_disks;
    //        dd_idx = (pd_idx + 1 + dd_idx) % raid_disks;
    //        ddf_layout = 1;
    //        break;

    //    case ALGORITHM_LEFT_ASYMMETRIC_6:
    //        /* RAID5 left_asymmetric, with Q on last device */
    //        pd_idx = data_disks - stripe_num % (raid_disks-1);
    //        if (dd_idx >= pd_idx)
    //                dd_idx++;
    //        qd_idx = raid_disks - 1;
    //        break;

    //    case ALGORITHM_RIGHT_ASYMMETRIC_6:
    //        pd_idx =stripe_num % (raid_disks-1);
    //        if (dd_idx >= pd_idx)
    //                dd_idx++;
    //        qd_idx = raid_disks - 1;
    //        break;

    //    case ALGORITHM_LEFT_SYMMETRIC_6:
    //        pd_idx = data_disks - stripe_num % (raid_disks-1);
    //        dd_idx = (pd_idx + 1 + dd_idx) % (raid_disks-1);
    //        qd_idx = raid_disks - 1;
    //        break;

    //    case ALGORITHM_RIGHT_SYMMETRIC_6:
    //        pd_idx = stripe_num % (raid_disks-1);
    //        dd_idx = (pd_idx + 1 + dd_idx) % (raid_disks-1);
    //        qd_idx = raid_disks - 1;
    //        break;

    //    case ALGORITHM_PARITY_0_6:
    //        pd_idx = 0;
    //        dd_idx++;
    //        qd_idx = raid_disks - 1;
    //        break;
    //    default:
    //        //BUG();
    //    }
    //    break;
    }

    stripe->dd_idx = dd_idx;
    stripe->pd_idx = pd_idx;
    //stripe->qd_idx = qd_idx;
    stripe->sector = (uint64_t)stripe_num * sectors_per_chunk + chunk_offset;

    return 0;
}

static void xorData(int cnt, size_t len, char inputs[cnt][len], size_t pos,
                    char **out) {
    if(cnt < 2)
        *out = inputs[0];

    for(int i=0; i<len; i++) {
        char result = inputs[0][i];
        for(int j=1; j<cnt; j++) {
            result = result ^ inputs[j][i];
        }
        (*out)[i+pos] = result;
    }
}

static void reconstructData(struct mddev *mddev, uint64_t start, size_t len,
                            char **out) {
    size_t pos = 0;

    int diskfds[mddev->raid_disks];
    for(int i=0; i<mddev->raid_disks; i++) {
        diskfds[i] = open(mddev->disks[i], O_RDONLY); 
        if(diskfds[i] < 0) {
            printf("Error opening %s: %s\n", mddev->disks[i], strerror(errno));
        }
    }

    while(pos < len) {
        int wantToRead = len-pos;
        struct stripe s;
        // Calculate logical sector
        uint64_t lsector = (start+pos)/512;
        // Calculate offset into sector
        uint64_t offset = (start+pos)-(lsector*512);
        getStripe(mddev, lsector, &s);

        int rc;
        char diskdata[mddev->raid_disks-1][512];
        for(int skip=0,i=0; i<mddev->raid_disks; i++) {
            if(i == s.dd_idx) {
                skip = 1;
                continue;
            }
            // Calculate physical position of data
            uint64_t diskpos = s.sector*512 + offset + mddev->offsets[i]*512;
            int willRead = 512-offset;
            if(willRead > wantToRead)
                willRead = wantToRead;
            rc = pread(diskfds[i], &diskdata[i-skip][0], willRead, diskpos);
            if(rc != willRead) {
                printf("Error reading disk %d: %s\n", i, strerror(errno));
                return;
            }
        }
        xorData(mddev->raid_disks-1, rc, diskdata, pos, out);
        pos += rc;
    }

    for(int i=0; i<mddev->raid_disks; i++) {
        close(diskfds[i]);
    }
}

int mdrepair(struct mddev *mddev, uint64_t start, size_t len, 
             uint32_t csumActual, uint32_t csumExpected) {
    printf("Actual checksum: %u\n", csumActual);
    printf("Expected checksum: %u\n", csumExpected);
    if(csumActual == csumExpected)
        return 0;

    char *reconstructedData = malloc(len);
    reconstructData(mddev, start, len, &reconstructedData);
    uint32_t csumReconstructed = alg_csumData(algsocket, reconstructedData, len);
    printf("Reconstructed data checksum: %u\n", csumReconstructed);

    if(csumReconstructed == csumExpected) {
        int fd = open(mddev->devnode, O_WRONLY);
        if(fd < 0) {
            printf("Error opening %s for writing: %s\n", mddev->devnode,
                   strerror(errno));
            return -1;
        }
        int rc = pwrite(fd, reconstructedData, len, start);
        if(rc != len) {
            printf("Error writing to %s: %s\n", mddev->devnode,
                   strerror(errno));
            return -1;
        }

        close(fd);
        sync();
    } else {
        return -1;
    }
    return 0;
}
