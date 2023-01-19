#include "overlay.h"
#include "mem.h"
#include <string.h>

static void buffer_append_opt(buffer_t, char *, size_t);

char *compute_overlay(config_t * config, size_t name_size, bool overlay2, size_t *upper_size, size_t *work_size)
{
    size_t lower_size = 0;
    if (config->lowerdir && *config->lowerdir) {
        lower_size = strlen(config->lowerdir);
    } else {
        config->lowerdir = "/";
        lower_size = 1;
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

    buffer_t buf = buffer_new(lower_size + *upper_size + *work_size + 64);
    if (!buf) {
        return NULL;
    }
    buffer_write_data(buf, 9, "lowerdir=");
    buffer_append_opt(buf, config->lowerdir, lower_size);
    buffer_write_data(buf, 10, ",upperdir=");
    buffer_append_opt(buf, config->upperdir, *upper_size);
    if (overlay2) {
        buffer_write_data(buf, 9, ",workdir=");
        buffer_append_opt(buf, config->workdir, *work_size);
    }
    buffer_write_byte(buf, 0);
    return buffer_use(buf);
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
