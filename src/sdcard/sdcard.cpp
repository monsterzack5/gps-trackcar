#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_ctrl.h>

LOG_MODULE_REGISTER(sdcard, CONFIG_TRACCAR_DEFAULT_LOG_LEVEL);

#ifdef CONFIG_TRACCAR_ENABLE_SD_CARD

#    include <ff.h>
#    include <zephyr/fs/fs.h>
#    include <zephyr/storage/disk_access.h>

#    define SDCARD_DISK_NAME "SD"
static FATFS fat_fs;
static struct fs_mount_t sd_mount {};

int sdcard_set_log_filtering()
{
    // NOTE: Backend names found via testing:
    // log_backend_fs
    // log_backend_uart

    const struct log_backend* fs_backend = NULL;
    const char correct_name[] = "log_backend_fs";

    // Find the backends by iterating through all registered backends
    for (int i = 0; i < log_backend_count_get(); i++) {
        const struct log_backend* backend = log_backend_get(i);
        // const char* name = log_backend_name_get(backend);

        LOG_INF("Found Backend: %s", backend->name);

        if (strncmp(backend->name, correct_name, sizeof(correct_name)) == 0) {
            fs_backend = backend;
            break;
        }
    }

    // Set filesystem backend to INF level for all modules
    if (fs_backend) {
        uint32_t rc = log_filter_set(fs_backend,
            0,  // Domain ID, This is a single domain project, so 0 is the default
            -1, // Module ID, this file is called 'sdcard', -1 for all modules
            LOG_LEVEL_INF);
        LOG_DBG("FS backend set to INF, rc = %u\n", rc);
    }
    return 0;
}

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
    int mkdir_rc = fs_mkdir("/SD:/logs");

    if (mkdir_rc != 0 && mkdir_rc != -EEXIST) {
        LOG_ERR("Failed to make logs directory. rc = %d", mkdir_rc);
    }

    // Short delay to let FS backend initialize
    k_sleep(K_MSEC(100));

    sdcard_set_log_filtering();

    return 0;
}
#else
int sdcard_init()
{
    LOG_INF("SD Card support disabled");
    return 0;
}
#endif