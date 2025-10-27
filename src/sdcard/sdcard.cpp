#include <ff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>

LOG_MODULE_REGISTER(sdcard, LOG_LEVEL_DBG);

#define SDCARD_DISK_NAME "SD"

static FATFS fat_fs;
static struct fs_mount_t sd_mount {};

int sdcard_init()
{

    sd_mount.type = FS_FATFS;
    sd_mount.fs_data = &fat_fs;
    sd_mount.mnt_point = "/SD:";

    LOG_INF("Initializing SD card...\n");

    // Initialize disk
    int ret = disk_access_init(SDCARD_DISK_NAME);
    if (ret) {
        LOG_ERR("SD card init failed: %d", ret);
        return ret;
    }

    ret = disk_access_status(SDCARD_DISK_NAME);
    if (ret != DISK_STATUS_OK) {
        LOG_ERR("Disk not ready, status: %d", ret);
        return -EIO;
    }

    k_sleep(K_MSEC(500));

    // Mount filesystem
    ret = fs_mount(&sd_mount);
    if (ret) {
        LOG_ERR("SD card mount failed: %d\n", ret);

        ret = fs_mkfs(FS_FATFS, (uintptr_t)SDCARD_DISK_NAME, NULL, 0);
        if (ret != 0) {
            LOG_ERR("SD card format failed: %d\n", ret);
            return ret;
        }

        ret = fs_mount(&sd_mount);
        if (ret != 0) {
            LOG_ERR("SD card mount failed after reformat: %d\n", ret);
            return ret;
        }
    }

    LOG_INF("SD card mounted successfully\n");

    // Create logs directory
    fs_mkdir("/SD:/logs");

    // Short delay to let FS backend initialize
    k_sleep(K_MSEC(100));

    return 0;
}