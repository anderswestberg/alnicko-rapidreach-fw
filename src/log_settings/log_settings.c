/**
 * @file log_settings.c
 * @brief Persistent log level settings management
 * 
 * This module saves and restores runtime log filter settings to/from
 * a JSON file in the filesystem. It hooks into the shell log commands
 * to automatically save settings when changed.
 */

#include "log_settings.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/data/json.h>
#include <zephyr/fs/fs.h>
#include <string.h>
#include "../file_manager/file_manager.h"

LOG_MODULE_REGISTER(log_settings, LOG_LEVEL_INF);

/* Define LOG_DOMAIN_ID if not defined */
#ifndef LOG_DOMAIN_ID
#define LOG_DOMAIN_ID 0
#endif

#define LOG_SETTINGS_FILE "/lfs/log_settings.json"
#define MAX_MODULE_NAME_LEN 32
#define MAX_LOG_MODULES 32

/* Structure to store log level for a module */
struct log_module_setting {
    char name[MAX_MODULE_NAME_LEN];
    uint8_t level;
};

/* Structure for the complete settings */
struct log_settings {
    uint8_t global_level;
    uint8_t module_count;
    struct log_module_setting modules[MAX_LOG_MODULES];
};

/* JSON descriptors */
static const struct json_obj_descr module_setting_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct log_module_setting, name, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct log_module_setting, level, JSON_TOK_NUMBER),
};

static const struct json_obj_descr log_settings_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct log_settings, global_level, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct log_settings, module_count, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_OBJ_ARRAY(struct log_settings, modules, MAX_LOG_MODULES, 
                             module_count, module_setting_descr, 
                             ARRAY_SIZE(module_setting_descr)),
};

/* Work item for delayed save */
static struct k_work_delayable save_work;
static bool settings_modified = false;

static void save_work_handler(struct k_work *work)
{
    int ret = log_settings_save();
    if (ret < 0) {
        LOG_ERR("Failed to save log settings: %d", ret);
    } else {
        LOG_DBG("Log settings saved successfully");
    }
    settings_modified = false;
}

int log_settings_save(void)
{
    struct log_settings settings;
    char json_buffer[1024];
    int ret;
    
    memset(&settings, 0, sizeof(settings));
    
    /* Get global log level */
    settings.global_level = log_filter_get(NULL, LOG_DOMAIN_ID, -1, true);
    
    /* Iterate through all log sources to get their levels */
    settings.module_count = 0;
    for (int i = 0; i < log_src_cnt_get(LOG_DOMAIN_ID); i++) {
        const char *name = log_source_name_get(LOG_DOMAIN_ID, i);
        uint32_t level = log_filter_get(NULL, LOG_DOMAIN_ID, i, true);
        
        /* Only save if different from global level */
        if (level != settings.global_level && settings.module_count < MAX_LOG_MODULES) {
            strncpy(settings.modules[settings.module_count].name, name, 
                    MAX_MODULE_NAME_LEN - 1);
            settings.modules[settings.module_count].level = level;
            settings.module_count++;
        }
    }
    
    /* Convert to JSON */
    ret = json_obj_encode_buf(log_settings_descr, ARRAY_SIZE(log_settings_descr),
                              &settings, json_buffer, sizeof(json_buffer));
    if (ret < 0) {
        LOG_ERR("Failed to encode settings to JSON: %d", ret);
        return ret;
    }
    
    /* Write to file */
    ret = file_manager_write(LOG_SETTINGS_FILE, json_buffer, strlen(json_buffer));
    if (ret < 0) {
        LOG_ERR("Failed to write settings file: %d", ret);
        return ret;
    }
    
    LOG_INF("Saved log settings: global=%d, modules=%d", 
            settings.global_level, settings.module_count);
    return 0;
}

int log_settings_load(void)
{
    struct log_settings settings;
    char json_buffer[1024];
    struct fs_file_t file;
    int ret;
    
    /* Check if settings file exists */
    if (file_manager_exists(LOG_SETTINGS_FILE) <= 0) {
        LOG_DBG("No saved log settings found");
        return 0;
    }
    
    /* Read the file */
    fs_file_t_init(&file);
    ret = fs_open(&file, LOG_SETTINGS_FILE, FS_O_READ);
    if (ret < 0) {
        LOG_ERR("Failed to open settings file: %d", ret);
        return ret;
    }
    
    ret = fs_read(&file, json_buffer, sizeof(json_buffer) - 1);
    fs_close(&file);
    
    if (ret < 0) {
        LOG_ERR("Failed to read settings file: %d", ret);
        return ret;
    }
    
    json_buffer[ret] = '\0';
    
    /* Parse JSON */
    ret = json_obj_parse(json_buffer, strlen(json_buffer),
                         log_settings_descr, ARRAY_SIZE(log_settings_descr),
                         &settings);
    if (ret < 0) {
        LOG_ERR("Failed to parse settings JSON: %d", ret);
        return ret;
    }
    
    /* Apply global level first */
    log_filter_set(NULL, LOG_DOMAIN_ID, -1, settings.global_level);
    LOG_INF("Restored global log level: %d", settings.global_level);
    
    /* Apply module-specific levels */
    for (int i = 0; i < settings.module_count; i++) {
        /* Find the source ID for this module name */
        for (int j = 0; j < log_src_cnt_get(LOG_DOMAIN_ID); j++) {
            const char *name = log_source_name_get(LOG_DOMAIN_ID, j);
            if (strcmp(name, settings.modules[i].name) == 0) {
                log_filter_set(NULL, LOG_DOMAIN_ID, j, settings.modules[i].level);
                LOG_INF("Restored %s log level: %d", 
                        settings.modules[i].name, settings.modules[i].level);
                break;
            }
        }
    }
    
    return 0;
}

int log_settings_clear(void)
{
    return file_manager_delete(LOG_SETTINGS_FILE);
}

/* Hook function to be called when log levels change */
/* TODO: Implement this when Zephyr provides a log change notification API */
#if 0
static void log_settings_changed(void)
{
    if (!settings_modified) {
        settings_modified = true;
        /* Schedule save after 1 second to batch multiple changes */
        k_work_schedule(&save_work, K_SECONDS(1));
    }
}
#endif

int log_settings_init(void)
{
    /* Initialize the delayed work item */
    k_work_init_delayable(&save_work, save_work_handler);
    
    /* Load saved settings */
    int ret = log_settings_load();
    if (ret < 0) {
        LOG_WRN("Failed to load saved settings: %d", ret);
    }
    
    /* TODO: Hook into log control to detect changes */
    /* For now, we'll need to manually call save from shell commands */
    
    return 0;
}
