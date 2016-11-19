#ifndef MDUTIL_H
#define MDUTIL_H

#include <stdint.h>
#include <sys/types.h>

#include "saturnd.h"

struct mddev {
    int layout;
    int level;
    int raid_disks;
    int chunk_size;

    char syspath[PATH_MAX];
    char devnode[PATH_MAX];
    char **disks; // Device node paths for component disks
    uint64_t *offsets; // Component disk data offsets (in sectors)
};

int initmddev(dev_t devnum, struct mddev *mddev);
int mdrepair(struct mddev *mddev, uint64_t start, size_t len,
             uint32_t csumActual, uint32_t csumExpected);

#endif
