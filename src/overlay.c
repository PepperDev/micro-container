#include "overlay.h"
#include "mem.h"
#include "io.h"
#include "proc.h"
#include <string.h>
#include <stdio.h>
#include <sys/mount.h>

static void buffer_append_opt(buffer_t, char *, size_t);

char *compute_overlay(config_t * config, size_t name_size, bool overlay2, size_t *upper_size, size_t *work_size,
                      size_t *lower_size)
{
    if (config->lowerdir && *config->lowerdir) {
        *lower_size = strlen(config->lowerdir);
    } else {
        config->lowerdir = "/";
        *lower_size = 1;
    }
    *upper_size = 0;
    *work_size = 0;
    if (!config->upperdir || (overlay2 && !config->workdir)) {
        size_t appdir_size;
        if (config->appdir) {
            appdir_size = strlen(config->appdir);
        } else {
            if (config->name) {
                appdir_size = name_size + 28;
                config->appdir = mem_append("/var/lib/microcontainer/app-", 28, config->name, name_size + 1, NULL, 0);
                if (!config->appdir) {
                    return NULL;
                }
            } else {
                config->appdir = "/var/lib/microcontainer/default";
                appdir_size = 31;
            }
        }
        if (config->upperdir) {
            *upper_size = strlen(config->upperdir);
        } else {
            config->upperdir = mem_path(config->appdir, appdir_size, "upper", 5, upper_size);
            if (!config->upperdir) {
                return NULL;
            }
        }
        if (overlay2) {
            if (config->workdir) {
                *work_size = strlen(config->workdir);
            } else {
                config->workdir = mem_path(config->appdir, appdir_size, "work", 4, work_size);
                if (!config->workdir) {
                    return NULL;
                }
            }
        }
    }

    buffer_t buf = buffer_new(*lower_size + *upper_size + *work_size + 64);
    if (!buf) {
        return NULL;
    }
    buffer_write_data(buf, 9, "lowerdir=");
    buffer_append_opt(buf, config->lowerdir, *lower_size);
    buffer_write_data(buf, 10, ",upperdir=");
    buffer_append_opt(buf, config->upperdir, *upper_size);
    if (overlay2) {
        buffer_write_data(buf, 9, ",workdir=");
        buffer_append_opt(buf, config->workdir, *work_size);
    }
    buffer_write_byte(buf, 0);
    return buffer_use(buf);
}

int overlay_filesystem(char *upper_dir, size_t upper_size, char *lower_dir, size_t lower_size)
{
    // TODO: use realpath?
    while (upper_size && upper_dir[upper_size - 1] == '/') {
        upper_size--;
    }
    while (lower_size && lower_dir[lower_size - 1] == '/') {
        lower_size--;
    }
    if (lower_size > upper_size) {
        return 0;
    }
    if (memcmp(lower_dir, upper_dir, lower_size)) {
        return 0;
    }
    if (upper_dir[lower_size] && upper_dir[lower_size] != '/') {
        return 0;
    }

    size_t i = upper_size - 1;
    while (i && upper_dir[i] != '/') {
        i--;
    }
    while (i && upper_dir[i] == '/') {
        i--;
    }
    while (i && upper_dir[i] != '/') {
        i--;
    }
    while (i && upper_dir[i] == '/') {
        i--;
    }
    if (!i) {
        fprintf(stderr, "Unable to find parent of %s.\n", upper_dir);
        return -1;
    }
    i++;
    upper_dir[i] = 0;
    if (io_mkdir(upper_dir, i)) {
        return -1;
    }
    int ret = io_samefs(lower_dir, upper_dir);
    if (ret == -1) {
        return -1;
    }
    if (ret) {
        upper_dir[i] = '/';
        return 0;
    }

    fprintf(stderr, "Lower filesystem is parent of upper filesystem, trying to mount loop...\n");

    char *img = mem_append(upper_dir, i, ".img", 5, NULL, 0);
    if (!img) {
        return -1;
    }

    ret = io_exists(img);
    if (ret == -1) {
        return -1;
    }

    bool mkfs = false;
    if (ret) {
        if (io_truncate(img, (off_t) 10 * 1024 * 1024 * 1024)) {
            return -1;
        }
        mkfs = true;
    } else {
        ret = io_blankfirststsector(img);
        if (ret == -1) {
            return -1;
        }
        if (!ret) {
            mkfs = true;
        }
    }

    if (mkfs) {
        char *args[] = {
            "mke2fs",
            "-Text4",
            "-m0",
            "-i65536",
            "-F",
            img,
            NULL
        };
        if (fork_and_exec(args)) {
            return -1;
        }
    }

    // losetup?
    if (mount(img, upper_dir, NULL, 0, NULL)) {
        fprintf(stderr, "Unable to mount image %s.\n", img);
        return -1;
    }
    upper_dir[i] = '/';
    return 0;
}

static void buffer_append_opt(buffer_t buf, char *data, size_t size)
{
    char *last = data;
    char *end = data + size;

    while (data < end) {
        if (*data == '\\' || *data == ',') {
            if (last < data) {
                buffer_write_data(buf, data - last, last);
            }
            buffer_write_byte(buf, '\\');
            buffer_write_byte(buf, *data);
            last = data + 1;
        }
        data++;
    }
    if (last < end) {
        buffer_write_data(buf, end - last, last);
    }
}
