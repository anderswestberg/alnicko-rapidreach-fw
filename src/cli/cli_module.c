/**
 * @file cli_module.c
 * @brief Shell interface for controlling system peripherals.
 *
 * This module provides shell commands grouped under the "rapidreach" root
 * command for controlling various. 
 * 
 * Each subsystem is represented as a subcommand tree and uses 
 * appropriate abstraction APIs.
 * 
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <stdlib.h>
#include <stdio.h>

LOG_MODULE_REGISTER(cli_module, LOG_LEVEL_INF);

#include "battery.h"
#include "charger.h"
#include "watchdog.h"
#include "microphone.h"
#include "led_control.h"

#include "audio_player.h"

#include "dev_info.h"
#include "switch_module.h"
#include "poweroff.h"
#include "rtc.h"

#ifdef CONFIG_RPR_MODEM
#include "modem_module.h"
#endif

#ifdef CONFIG_RPR_ETHERNET
#include "ethernet.h"
#endif

#ifdef CONFIG_RPR_WIFI
#include "wifi.h"
#define WIFI_CONFIG_JSON_MAX_LEN 256
#endif

#ifdef CONFIG_RPR_MODULE_HTTP
#include "http_module.h"
#endif

#ifdef CONFIG_RPR_MODULE_DFU
#include "dfu_manager.h"
#endif

#ifdef CONFIG_RPR_MODULE_MQTT
#include "../mqtt_module/mqtt_module.h"
#endif
#ifdef CONFIG_RPR_MQTT_LOG_CLIENT
#include "../mqtt_log_client/mqtt_log_client.h"
#endif
#ifdef CONFIG_RPR_MODULE_FILE_MANAGER
#include "../file_manager/file_manager.h"
#endif

#ifdef CONFIG_EXAMPLES_ENABLE_MAIN_EXAMPLES
#include "power_supervisor.h"
#endif

#define FULL_FILE_PATH_MAX_LEN \
    (CONFIG_RPR_FOLDER_PATH_MAX_LEN + CONFIG_RPR_FILENAME_MAX_LEN)

/**
 * @brief Command to set the RTC date and time.
 * 
 * Usage: rtc set <year> <month> <day> <hour> <min> <sec>
 * 
 * @param sh    Pointer to the shell context.
 * @param argc  Number of input arguments (should be 7, including "set").
 * @param argv  Array of input arguments. argv[1] to argv[6] should contain date and time.
 *
 * @return 0 on success, negative error code on failure.
 */
static int cmd_rtc_set(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 7) {
        shell_error(sh,
                    "Usage: rtc set <year> <month> <day> <hour> <min> <sec>");
        return -EINVAL;
    }

    struct rtc_time t;

    t.tm_year = atoi(argv[1]);
    t.tm_mon  = atoi(argv[2]);
    t.tm_mday = atoi(argv[3]);
    t.tm_hour = atoi(argv[4]);
    t.tm_min  = atoi(argv[5]);
    t.tm_sec  = atoi(argv[6]);

    if (t.tm_year < 1900 || t.tm_mon < 1 || t.tm_mon > 12 || t.tm_mday < 1 ||
        t.tm_mday > 31 || t.tm_hour < 0 || t.tm_hour > 23 || t.tm_min < 0 ||
        t.tm_min > 59 || t.tm_sec < 0 || t.tm_sec > 59) {
        shell_error(sh, "Invalid date/time values");
        return -EINVAL;
    }

    int ret = set_date_time(&t);
    if (ret < 0) {
        shell_error(sh, "Failed to set RTC time: %d", ret);
        return ret;
    }

    shell_print(sh, "RTC time successfully set.");
    return 0;
}

/**
 * @brief Command to get and display the current RTC date and time.
 */
static int cmd_rtc_get(const struct shell *sh, size_t argc, char **argv)
{
    struct rtc_time t;
    int             ret = get_date_time(&t);
    if (ret < 0) {
        shell_error(sh, "Failed to get RTC time: %d", ret);
        return ret;
    }

    shell_print(sh,
                "Current RTC time: %04d-%02d-%02d %02d:%02d:%02d",
                t.tm_year,
                t.tm_mon,
                t.tm_mday,
                t.tm_hour,
                t.tm_min,
                t.tm_sec);

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
        rtc_cmds,
        SHELL_CMD_ARG(
                set,
                NULL,
                "Set RTC time: rtc set <year> <mon> <day> <hour> <min> <sec>",
                cmd_rtc_set,
                7,
                0),
        SHELL_CMD(get, NULL, "Get RTC time", cmd_rtc_get),
        SHELL_SUBCMD_SET_END);

#ifdef CONFIG_RPR_WIFI
/**
 * @brief Extracts a string value from a JSON string by key.
 *
 * @param json      Pointer to the input JSON string.
 * @param key       The key whose string value should be extracted.
 * @param out       Output buffer to store the extracted value.
 * @param out_size  Size of the output buffer.
 *
 * @return true if the value was successfully extracted, false otherwise.
 */
static bool
extract_value(const char *json, const char *key, char *out, size_t out_size)
{
    char *key_pos = strstr(json, key);
    if (!key_pos)
        return false;

    char *quote1 = strchr(key_pos + 1, '\"');
    if (!quote1)
        return false;

    quote1 = strchr(quote1 + 1, '\"');
    if (!quote1)
        return false;
    char *quote2 = strchr(quote1 + 1, '\"');
    if (!quote2)
        return false;

    size_t len = quote2 - quote1 - 1;
    if (len >= out_size)
        len = out_size - 1;

    strncpy(out, quote1 + 1, len);
    out[len] = '\0';
    return true;
}

/**
 * @brief Extracts the integer value of the "band" field from a JSON string.
 *
 * @param json Pointer to the input JSON string.
 * @param band Pointer to an integer where the extracted band value will be stored.
 *
 * @return true if the value was successfully extracted, false otherwise.
 */
static bool extract_band(const char *json, int *band)
{
    const char *band_key = "\"band\":";
    char       *pos      = strstr(json, band_key);
    if (!pos)
        return false;

    pos += strlen(band_key);
    while (*pos == ' ')
        pos++;

    *band = atoi(pos);
    return true;
}

/**
 * @brief Callback to search for saved Wi-Fi network configuration by SSID.
 *
 * This function is used during autoconnect to determine whether the scanned SSID
 * matches a saved configuration. If a match is found, the corresponding configuration
 * is written to the output parameter.
 *
 * @param ssid      SSID of the scanned Wi-Fi network.
 * @param out_cfg   Pointer to the structure where the matched configuration
 *                  should be written, if found.
 *
 * @return true if a saved configuration is found for the given SSID, false otherwise.
 */
static bool saved_network_lookup_cb(const char              *ssid,
                                    struct wifi_par_context *out_cfg)
{
    struct fs_dir_t  dir;
    struct fs_dirent entry;
    char             full_path[FULL_FILE_PATH_MAX_LEN];
    bool             found = false;

    fs_dir_t_init(&dir);
    if (fs_opendir(&dir, CONFIG_RPR_WIFI_CONFIG_DEFAULT_PATH) != 0) {
        return false;
    }

    while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != 0) {
        if (entry.type == FS_DIR_ENTRY_FILE && strcmp(entry.name, ssid) == 0) {
            snprintf(full_path,
                     sizeof(full_path),
                     "%s/%s",
                     CONFIG_RPR_WIFI_CONFIG_DEFAULT_PATH,
                     entry.name);
            found = true;
            break;
        }
    }

    fs_closedir(&dir);
    if (!found)
        return false;

    struct fs_file_t file;
    fs_file_t_init(&file);
    if (fs_open(&file, full_path, FS_O_READ) < 0) {
        return false;
    }

    char    buf[WIFI_CONFIG_JSON_MAX_LEN] = { 0 };
    ssize_t read = fs_read(&file, buf, sizeof(buf) - 1);
    fs_close(&file);
    if (read <= 0)
        return false;

    if (!extract_value(buf, "\"ssid\"", out_cfg->ssid, sizeof(out_cfg->ssid)))
        return false;
    if (!extract_value(buf, "\"psk\"", out_cfg->psk, sizeof(out_cfg->psk)))
        return false;

    int band = 0;
    if (!extract_band(buf, &band))
        return false;
    out_cfg->band = (enum wifi_frequency_bands)band;

    return true;
}
#endif

/**
 * @brief CLI command to show if Wi-Fi is the default interface.
 * (only if CONFIG_RPR_WIFI is enabled)
 */
static int cmd_wifi_status(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_WIFI

    if (is_wifi_iface_default()) {
        shell_print(sh, "Wi-Fi is set as the default network interface.");
    } else {
        shell_print(sh, "Wi-Fi is NOT the default network interface.");
    }
#else
    shell_info(sh, "Set CONFIG_RPR_WIFI to enable WIFI support.");
#endif
    return 0;
}

/**
 * @brief CLI command to set Wi-Fi as the default interface.
 * (only if CONFIG_RPR_WIFI is enabled)
 */
static int
cmd_wifi_set_default(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_WIFI
    wifi_status_t result = wifi_set_iface_default();

    switch (result) {
    case WIFI_OK:
        shell_print(sh, "Wi-Fi interface set as default.");
        break;
    case WIFI_ERR_IFACE_NOT_FOUND:
        shell_error(sh, "No Wi-Fi interface found.");
        break;
    default:
        shell_error(sh, "Unknown error occurred.");
        break;
    }

    return result;
#else
    shell_info(sh, "Set CONFIG_RPR_WIFI to enable WIFI support.");
#endif
    return 0;
}

/**
 * @brief Wi-Fi connection shell command handler.
 *
 * Allows manual connection to a Wi-Fi network or autoconnect to a saved one.
 * 
 * Usage:
 *   - wifi connect               → start autoconnect with saved configs
 *   - wifi connect <SSID>        → connect to SSID with empty password and default band
 *   - wifi connect <SSID> <PSK>  → connect to SSID with password and default band
 *   - wifi connect <SSID> <PSK> <Band> → full manual connect (band: 2/5/6)
 */
static int cmd_wifi_connect(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_WIFI

    wifi_status_t           status = WIFI_OK;
    struct wifi_par_context ctx    = { .ssid = "",
                                       .psk  = "",
                                       .band = WIFI_FREQ_BAND_2_4_GHZ };

    if (argc >= 2) {
        size_t len = strlen(argv[1]);
        if (len > WIFI_SSID_MAX_LEN) {
            shell_error(sh,
                        "SSID is too long (max %d characters)",
                        WIFI_SSID_MAX_LEN);
            return -EINVAL;
        }
        strncpy(ctx.ssid, argv[1], len);
        ctx.ssid[len] = '\0';
    }

    if (argc >= 3) {
        size_t len = strlen(argv[2]);
        if (len > WIFI_PSK_MAX_LEN) {
            shell_error(sh,
                        "Password is too long (max %d characters)",
                        WIFI_PSK_MAX_LEN);
            return -EINVAL;
        }
        strncpy(ctx.psk, argv[2], len);
        ctx.psk[len] = '\0';
    }

    if (argc >= 4) {
        int band = atoi(argv[3]);
        switch (band) {
        case 2:
            ctx.band = WIFI_FREQ_BAND_2_4_GHZ;
            break;
        case 5:
            ctx.band = WIFI_FREQ_BAND_5_GHZ;
            break;
        case 6:
            ctx.band = WIFI_FREQ_BAND_6_GHZ;
            break;
        default:
            shell_error(sh, "Invalid band value. Use 2, 5, or 6.");
            return -EINVAL;
        }
    }

    if (argc == 1) {
        shell_print(sh, "Starting autoconnect...");
        status = wifi_start_autoconnect(saved_network_lookup_cb);
    } else {
        shell_print(sh, "Starting connect...");

        status = wifi_start_connect(&ctx);
    }

    if (status != WIFI_OK) {
        switch (status) {
        case WIFI_WRN_ALREADY_CONNECTED:
            shell_warn(
                    sh,
                    "Wi-Fi is already connected. Disconnect to switch to another network");
            break;
        case WIFI_ERR_INVALID_VALUE:
            shell_warn(sh,
                       "Invalid Wi-Fi parameters (SSID or password too long)");
            break;
        case WIFI_ERR_IFACE_NOT_FOUND:
            shell_warn(sh, "Wi-Fi interface not found");
            break;
        case WIFI_ERR_REQUEST_TIMEOUT:
            shell_warn(sh, "Wi-Fi request timed out");
            break;
        case WIFI_ERR_REQUEST_FAIL:
            shell_warn(sh, "Wi-Fi request failed (internal error)");
            break;
        default:
            shell_warn(sh, "Wi-Fi connection failed with status: %d", status);
            break;
        }
        return -EIO;
    }

    shell_print(sh, "Wi-Fi connect request successfully sent.");
#else
    shell_info(sh, "Set CONFIG_RPR_WIFI to enable WIFI support.");
#endif
    return 0;
}

/**
 * @brief Disconnect from the currently connected Wi-Fi network.
 */
static int cmd_wifi_disconnect(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_WIFI

    wifi_status_t status = wifi_start_disconnect();

    if (status != WIFI_OK) {
        switch (status) {
        case WIFI_ERR_IFACE_NOT_FOUND:
            shell_warn(sh, "Wi-Fi interface not found");
            break;
        case WIFI_ERR_REQUEST_TIMEOUT:
            shell_warn(sh, "Wi-Fi request timed out");
            break;
        case WIFI_ERR_REQUEST_FAIL:
            shell_warn(sh, "Wi-Fi request failed (internal error)");
            break;
        case WIFI_WRN_NOT_CONNECTED:
            shell_warn(sh, "Wi-Fi is not connected");
            break;
        default:
            shell_warn(sh, "Wi-Fi connection failed with status: %d", status);
            break;
        }
        return -EIO;
    }

    shell_print(sh, "Wi-Fi disconnect request successfully sent.");
#else
    shell_info(sh, "Set CONFIG_RPR_WIFI to enable WIFI support.");
#endif
    return 0;
}

/**
 * @brief Start a Wi-Fi network scan.
 */
static int cmd_wifi_scan(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_WIFI

    wifi_status_t status = wifi_start_scan();

    if (status != WIFI_OK) {
        switch (status) {
        case WIFI_ERR_IFACE_NOT_FOUND:
            shell_warn(sh, "Wi-Fi interface not found");
            break;
        case WIFI_ERR_REQUEST_TIMEOUT:
            shell_warn(sh, "Wi-Fi request timed out");
            break;
        case WIFI_ERR_REQUEST_FAIL:
            shell_warn(sh, "Wi-Fi request failed (internal error)");
            break;
        default:
            shell_warn(sh, "Wi-Fi connection failed with status: %d", status);
            break;
        }
        return -EIO;
    }

    shell_print(sh, "Wi-Fi scan request successfully sent.");
#else
    shell_info(sh, "Set CONFIG_RPR_WIFI to enable WIFI support.");
#endif
    return 0;
}

/**
 * @brief Display current Wi-Fi connection information.
 */
static int cmd_wifi_info(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_WIFI

    struct wifi_iface_status status = { 0 };
    wifi_status_t            ret    = wifi_get_info(&status);

    if (ret == WIFI_WRN_NOT_CONNECTED) {
        shell_warn(sh, "Wi-Fi is not connected");
        return 0;
    } else if (ret != WIFI_OK) {
        shell_error(sh, "Failed to get Wi-Fi info");
        switch (ret) {
        case WIFI_ERR_INVALID_VALUE:
            shell_warn(sh, "Invalid Wi-Fi status structure");
            break;
        case WIFI_ERR_IFACE_NOT_FOUND:
            shell_warn(sh, "Wi-Fi interface not found");
            break;
        case WIFI_ERR_REQUEST_TIMEOUT:
            shell_warn(sh, "Wi-Fi request timed out");
            break;
        case WIFI_ERR_REQUEST_FAIL:
            shell_warn(sh, "Wi-Fi request failed (internal error)");
            break;
        default:
            shell_warn(sh, "Wi-Fi connection failed with status: %d", ret);
            break;
        }
        return -EIO;
    }

    shell_print(sh, "SSID: %s", status.ssid);
    shell_print(sh, "Band: %s", wifi_band_txt(status.band));
    shell_print(sh, "Channel: %d", status.channel);
    shell_print(sh, "Security: %s", wifi_security_txt(status.security));
    shell_print(sh, "RSSI: %d", status.rssi);

#else
    shell_info(sh, "Set CONFIG_RPR_WIFI to enable WIFI support.");
#endif
    return 0;
}

/**
 * @brief Save current Wi-Fi connection parameters to file.
 */
static int cmd_wifi_save(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_WIFI
    struct wifi_par_context ctx    = { 0 };
    wifi_status_t           status = wifi_get_current_param(&ctx);

    if (status == WIFI_WRN_NOT_CONNECTED) {
        shell_warn(sh, "Wi-Fi is not connected");
        return -EINVAL;
    } else if (status == WIFI_ERR_INVALID_VALUE) {
        shell_error(sh, "Invalid Wi-Fi context");
        return -EINVAL;
    } else if (status != WIFI_OK) {
        shell_error(sh, "Unexpected error while getting Wi-Fi parameters");
        return -EIO;
    }

    // Check file name length
    if (strlen(ctx.ssid) > CONFIG_RPR_FILENAME_MAX_LEN) {
        shell_error(sh,
                    "SSID is too long (max %d characters)",
                    CONFIG_RPR_FILENAME_MAX_LEN);
        return -ENAMETOOLONG;
    }

    // Build full path
    char full_path[FULL_FILE_PATH_MAX_LEN];
    snprintf(full_path,
             sizeof(full_path),
             "%s/%s",
             CONFIG_RPR_WIFI_CONFIG_DEFAULT_PATH,
             ctx.ssid);

    char json[WIFI_CONFIG_JSON_MAX_LEN];
    int  len = snprintf(json,
                       sizeof(json),
                       "{\n"
                        "\"ssid\":\"%s\",\n"
                        "\"psk\":\"%s\",\n"
                        "\"band\":%d\n"
                        "}\n",
                       ctx.ssid,
                       ctx.psk,
                       (int)ctx.band);

    if (len < 0 || len >= sizeof(json)) {
        shell_error(sh, "Failed to create JSON string");
        return -EIO;
    }

    // Write to file
    struct fs_file_t file;
    fs_file_t_init(&file);

    int ret = fs_open(&file, full_path, FS_O_WRITE | FS_O_CREATE | FS_O_TRUNC);
    if (ret < 0) {
        shell_error(sh, "Failed to open file: %s (err %d)", full_path, ret);
        return -EIO;
    }

    ret = fs_write(&file, json, len);
    fs_close(&file);

    if (ret < 0) {
        shell_error(sh, "Failed to write to file: %s (err %d)", full_path, ret);
        return -EIO;
    }

    shell_print(sh, "Wi-Fi config for SSID '%s' saved.", ctx.ssid);

#else
    shell_info(sh, "Set CONFIG_RPR_WIFI to enable WIFI support.");
#endif
    return 0;
}

/**
 * @brief List all saved Wi-Fi network configurations.
 */
static int cmd_wifi_saved_show(const struct shell *sh, size_t argc, char **argv)
{
    struct fs_dir_t  dir;
    struct fs_dirent entry;
    int              index = 1;

    fs_dir_t_init(&dir);
    if (fs_opendir(&dir, CONFIG_RPR_WIFI_CONFIG_DEFAULT_PATH) != 0) {
        shell_error(sh, "Failed to open directory");
        return -ENOENT;
    }

    while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != 0) {
        if (entry.type == FS_DIR_ENTRY_FILE) {
            shell_print(sh, "%d: %s", index++, entry.name);
        }
    }

    fs_closedir(&dir);
    return 0;
}

/**
 * @brief Delete a saved Wi-Fi configuration by index or delete all.
 */
static int
cmd_wifi_saved_delete(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: saved delete <index|all>");
        return -EINVAL;
    }

    struct fs_dir_t  dir;
    struct fs_dirent entry;
    char             full_path[FULL_FILE_PATH_MAX_LEN];
    int              index        = 1;
    int              target_index = -1;
    bool             delete_all   = false;
    bool             file_deleted = false;

    fs_dir_t_init(&dir);

    if (fs_opendir(&dir, CONFIG_RPR_WIFI_CONFIG_DEFAULT_PATH) != 0) {
        shell_error(sh, "Failed to open directory");
        return -ENOENT;
    }

    if (strcmp(argv[1], "all") == 0) {
        delete_all = true;
    } else {
        target_index = atoi(argv[1]);
        if (target_index <= 0) {
            shell_error(sh, "Invalid index value");
            return -EINVAL;
        }
    }

    while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != 0) {
        if (entry.type == FS_DIR_ENTRY_FILE) {
            if (delete_all || index == target_index) {
                snprintf(full_path,
                         sizeof(full_path),
                         "%s/%s",
                         CONFIG_RPR_WIFI_CONFIG_DEFAULT_PATH,
                         entry.name);
                fs_unlink(full_path);
                shell_print(sh, "Deleted: %s", entry.name);
                file_deleted = true;

                if (!delete_all) {
                    break;
                }
            }
            index++;
        }
    }

    fs_closedir(&dir);

    if (delete_all && file_deleted) {
        shell_print(sh, "All saved Wi-Fi networks deleted.");
    }

    if (!file_deleted) {
        shell_warn(sh, "Saved Wi-Fi networks not found or nothing deleted.");
        return -ENOENT;
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
        wifi_default_cmds,
        SHELL_CMD(get,
                  NULL,
                  "Check if Wi-Fi is the default interface",
                  cmd_wifi_status),
        SHELL_CMD(set,
                  NULL,
                  "Set Wi-Fi as the default interface",
                  cmd_wifi_set_default),
        SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
        wifi_saved_cmds,
        SHELL_CMD(show, NULL, "Show saved networks", cmd_wifi_saved_show),
        SHELL_CMD(delete,
                  NULL,
                  "Delete saved network by index or all networks",
                  cmd_wifi_saved_delete),
        SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
        wifi_cmds,
        SHELL_CMD(scan, NULL, "Start Wi-Fi scan", cmd_wifi_scan),
        SHELL_CMD_ARG(
                connect,
                NULL,
                "Connect using provided params: [ssid] [pass] [band]; use defaults or autoconnect if missing",
                cmd_wifi_connect,
                1,
                3),
        SHELL_CMD(disconnect, NULL, "Disconnect Wi-Fi", cmd_wifi_disconnect),
        SHELL_CMD(info, NULL, "Wi-Fi info", cmd_wifi_info),
        SHELL_CMD(save, NULL, "Save current network", cmd_wifi_save),
        SHELL_CMD(saved, &wifi_saved_cmds, "Manage saved networks", NULL),
        SHELL_CMD(default, &wifi_default_cmds, "Wi-Fi interface control", NULL),
        SHELL_SUBCMD_SET_END);

/**
 * @brief Request a one-time firmware upgrade. Need to confirm or revert the changes during the next boot.
 */
static int
cmd_dfu_upgrade_once(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODULE_DFU
    int ret = dfu_request_upgrade();
    if (ret == 0) {
        shell_print(sh, "Firmware upgrade requested. Reboot to apply.");
    } else {
        shell_error(sh, "Failed to request firmware upgrade (code: %d)", ret);
    }
#else
    shell_info(sh, "Set CONFIG_RPR_MODULE_DFU to enable DFU support.");
#endif
    return 0;
}

/**
 * @brief Request a permanent firmware upgrade. Automatically confirmed
 * without requiring manual confirmation after reboot.
 */
static int
cmd_dfu_upgrade_permanent(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODULE_DFU
    int ret = dfu_request_upgrade_with_confirmed_img();
    if (ret == 0) {
        shell_print(sh,
                    "Permanent firmware upgrade requested. Reboot to apply.");
    } else {
        shell_error(sh, "Failed to request permanent upgrade (code: %d)", ret);
    }
#else
    shell_info(sh, "Set CONFIG_RPR_MODULE_DFU to enable DFU support.");
#endif
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
        sub_dfu_upgrade,
        SHELL_CMD(once,
                  NULL,
                  "Run image once, then confirm or revert",
                  cmd_dfu_upgrade_once),
        SHELL_CMD(permanent,
                  NULL,
                  "Run and confirm image permanently",
                  cmd_dfu_upgrade_permanent),
        SHELL_SUBCMD_SET_END);

/**
 * @brief CLI command to display the result of the last firmware image integrity check.
 */
static int cmd_dfu_integrity(const struct shell *sh, size_t argc, char **argv)
{
#if defined(CONFIG_RPR_MODULE_DFU) && defined(CONFIG_RPR_IMAGE_INTEGRITY_CHECK)
    dfu_upd_img_integrity_t status = dfu_get_upd_img_integrity();

    switch (status) {
    case UPD_IMG_INTEGRITY_UNVERIFIED:
        shell_warn(sh, "Update image integrity has not been verified.");
        break;
    case UPD_IMG_INTEGRITY_VALID:
        shell_print(sh, "Update image integrity is VALID.");
        break;
    case UPD_IMG_INTEGRITY_INVALID:
        shell_error(sh, "Update image integrity is INVALID!");
        break;
    default:
        shell_error(sh, "Unknown integrity status.");
        break;
    }
#else
    shell_info(
            sh,
            "Set CONFIG_RPR_MODULE_DFU to enable DFU support.\n"
            "Set CONFIG_RPR_IMAGE_INTEGRITY_CHECK to enable image integrity check support.");
#endif
    return 0;
}

/**
 * @brief Confirms the currently running image as valid.
 */
static int cmd_dfu_confir_mark(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODULE_DFU

    int ret = dfu_mark_current_img_as_confirmed();

    if (ret == 0) {
        shell_print(sh, "Current image successfully confirmed.");
    } else {
        shell_error(sh, "Failed to confirm current image (code: %d)", ret);
    }

    return ret;
#else
    shell_info(sh, "Set CONFIG_RPR_MODULE_DFU to enable DFU support.");
#endif
    return 0;
}

/**
 * @brief Checks if the currently running image is confirmed.
 */
static int
cmd_dfu_confir_check(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODULE_DFU

    int ret = dfu_is_current_img_confirmed();

    if (ret < 0) {
        shell_error(
                sh, "Error checking image confirmation status (code: %d)", ret);
    } else if (ret == 1) {
        shell_print(sh, "Current image is confirmed.");
    } else {
        shell_warn(sh, "Current image is NOT confirmed.");
    }

#else
    shell_info(sh, "Set CONFIG_RPR_MODULE_DFU to enable DFU support.");
#endif
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_dfu_confir,
                               SHELL_CMD(mark,
                                         NULL,
                                         "Mark current image as confirmed",
                                         cmd_dfu_confir_mark),
                               SHELL_CMD(check,
                                         NULL,
                                         "Check if current image is confirmed",
                                         cmd_dfu_confir_check),
                               SHELL_SUBCMD_SET_END);

/**
 * @brief Print the firmware update version stored in the DFU slot.
 */
static int cmd_dfu_version(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODULE_DFU

    char version[UPD_VERSION_STRING_MAX_LEN];

    if (dfu_get_fw_update_version_str(version, sizeof(version))) {
        shell_print(sh, "Firmware update version: %s", version);
    } else {
        shell_error(sh, "Failed to read firmware version from DFU slot.");
    }

#else
    shell_info(sh, "Set CONFIG_RPR_MODULE_DFU to enable DFU support.");
#endif
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
        sub_dfu,
        SHELL_CMD(version,
                  NULL,
                  "Show firmware update version from DFU slot",
                  cmd_dfu_version),
        SHELL_CMD(confirm, &sub_dfu_confir, "Manage image confirmation", NULL),
        SHELL_CMD(upgrade,
                  &sub_dfu_upgrade,
                  "Request upgrade from DFU image slot",
                  NULL),
        SHELL_CMD(integrity,
                  NULL,
                  "Show integrity check result for the firmware update image",
                  cmd_dfu_integrity),
        SHELL_SUBCMD_SET_END);

/**
 * @brief CLI command to show if Ethernet is the default interface.
 * (only if CONFIG_RPR_ETHERNET is enabled)
 */
static int cmd_eth_status(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_ETHERNET

    if (is_ethernet_iface_default()) {
        shell_print(sh, "Ethernet is set as the default network interface.");
    } else {
        shell_print(sh, "Ethernet is NOT the default network interface.");
    }
#else
    shell_info(sh, "Set CONFIG_RPR_ETHERNET to enable ethernet support.");
#endif
    return 0;
}

/**
 * @brief CLI command to set Ethernet as the default interface.
 * (only if CONFIG_RPR_ETHERNET is enabled)
 */
static int cmd_eth_set_default(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_ETHERNET
    ethernet_status_t result = ethernet_set_iface_default();

    switch (result) {
    case ETHERNET_OK:
        shell_print(sh, "Ethernet interface set as default.");
        break;
    case ETHERNET_ERR_NO_INTERFACE:
        shell_error(sh, "No Ethernet interface found.");
        break;
    default:
        shell_error(sh, "Unknown error occurred.");
        break;
    }

    return result;
#else
    shell_info(sh, "Set CONFIG_RPR_ETHERNET to enable ethernet support.");
#endif
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
        eth_default_cmds,
        SHELL_CMD(get,
                  NULL,
                  "Check if ethernet is the default interface",
                  cmd_eth_status),
        SHELL_CMD(set,
                  NULL,
                  "Set ethernet as the default interface",
                  cmd_eth_set_default),
        SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_eth,
                               SHELL_CMD(default,
                                         &eth_default_cmds,
                                         "Ethernet interface control",
                                         NULL),
                               SHELL_SUBCMD_SET_END);

/**
 * @brief Shell command to shutdown process.
 */
static int cmd_poweroff(const struct shell *sh, size_t argc, char **argv)
{

    shell_print(sh, "Shutdown...");

#ifdef CONFIG_EXAMPLES_ENABLE_MAIN_EXAMPLES
    request_poweroff();
    return 0;
#endif

    k_msleep(500);
    int ret = poweroff_activate();
    if (ret < 0) {
        shell_error(sh, "Failed to activate poweroff(err %d)", ret);
        return ret;
    }
    return 0;
}

/**
 * @brief Shell command to read the state of a specific switch.
 */
static int cmd_switch_read(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: switch read <index 0-3>");
        return -EINVAL;
    }

    int index = atoi(argv[1]);
    if (index < 0 || index > 3) {
        shell_error(sh, "Invalid switch index. Must be 0 to 3.");
        return -EINVAL;
    }

    int state = switch_get_state((uint8_t)index);
    if (state < 0) {
        shell_error(sh, "Failed to read switch %d (err %d)", index, state);
        return state;
    }

    shell_print(sh, "Switch %d state: %s", index, state ? "ON" : "OFF");
    return 0;
}

/**
 * @brief Starts audio playback of the file by index from the playlist.
 */
static int cmd_audio_play(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: play <index>");
        return -EINVAL;
    }

    int target_index = atoi(argv[1]);
    if (target_index <= 0) {
        shell_error(sh, "Invalid index value");
        return -EINVAL;
    }

    struct fs_dir_t  dir;
    struct fs_dirent entry;
    fs_dir_t_init(&dir);

    if (fs_opendir(&dir, CONFIG_RPR_AUDIO_DEFAULT_PATH) != 0) {
        shell_error(sh, "Failed to open directory");
        return -ENOENT;
    }

    int  index      = 1;
    bool file_found = false;
    char full_path[FULL_AUDIO_PATH_MAX_LEN];

    while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != 0) {
        if (entry.type == FS_DIR_ENTRY_FILE) {
            if (index == target_index) {
                snprintf(full_path,
                         sizeof(full_path),
                         "%s/%s",
                         CONFIG_RPR_AUDIO_DEFAULT_PATH,
                         entry.name);
                file_found = true;
                break;
            }
            index++;
        }
    }

    fs_closedir(&dir);

    if (!file_found) {
        shell_error(sh, "File with index %d not found", target_index);
        return -ENOENT;
    }

    player_status_t status = -1;

    do {

        status = audio_player_start(full_path);

        if (status != PLAYER_OK)
            break;

        k_msleep(100);
    } while (!get_playing_status());

    switch (status) {
    case PLAYER_OK:
        shell_print(sh, "Audio playback started successfully");
        break;
    case PLAYER_ERROR_CODEC_INIT:
        shell_error(sh, "Error: Audio device is not initialized");
        break;
    case PLAYER_ERROR_BUSY:
        shell_error(sh, "Error: Audio device is already playing");
        break;
    case PLAYER_EMPTY_DATA:
        shell_error(sh, "Error: Provided Opus data is empty or invalid");
        break;
    default:
        shell_error(sh, "Error: Unknown playback error (code %d)", status);
        break;
    }
    return (status == PLAYER_OK) ? 0 : -EINVAL;
}

/**
 * @brief Stops currently playing audio.
 */
static int cmd_audio_stop(const struct shell *sh, size_t argc, char **argv)
{
    player_status_t status = audio_player_stop();

    switch (status) {
    case PLAYER_OK:
        shell_print(sh, "Audio playback stopped");
        break;
    case PLAYER_ERROR_CODEC_INIT:
        shell_error(sh, "Error: Audio device is not initialized");
        break;
    default:
        shell_error(sh, "Error: Unknown stop error (code %d)", status);
        break;
    }

    return (status == PLAYER_OK) ? 0 : -EINVAL;
}

/**
 * @brief Toggles pause/resume state of playback.
 */
static int cmd_audio_pause(const struct shell *sh, size_t argc, char **argv)
{

    bool pause_state = !(get_pause_status());

    if (!get_playing_status()) {
        shell_warn(sh, "Audio is not playing, cannot pause");
        return -EINVAL;
    }

    player_status_t status = audio_player_pause(pause_state);

    switch (status) {
    case PLAYER_OK:
        shell_print(sh, "Audio %s", pause_state ? "paused" : "resumed");
        break;
    case PLAYER_ERROR_CODEC_INIT:
        shell_error(sh, "Error: Audio device is not initialized");
        break;
    default:
        shell_error(sh,
                    "Unknown error during %s operation",
                    pause_state ? "pause" : "resume");
        break;
    }
    return (status == PLAYER_OK) ? 0 : -EPERM;
}

/**
 * @brief Displays current audio status including playback, pause, mute, and volume level.
 */
static int cmd_audio_info(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "Audio info:");
    shell_print(sh,
                "  Playback status : %s",
                get_playing_status() ? "Playing" : "Stopped");

    shell_print(sh,
                "  Pause status    : %s",
                get_pause_status() ? "Paused" : "Not paused");

    shell_print(sh, "  Volume level    : %d", get_volume());

    shell_print(sh,
                "  Mute status     : %s",
                get_mute_status() ? "Muted" : "Not muted");

    return 0;
}

/**
 * @brief Sets the volume level of the audio output.
 * 
 * @param shell Shell context.
 * @param argc  Number of command arguments.
 * @param argv  Array of command arguments. argv[1] must be a valid integer.
 * @retval 0    Volume set successfully.
 */
static int cmd_audio_volume(const struct shell *sh, size_t argc, char **argv)
{
    int level = atoi(argv[1]);

    if (level < MINIMUM_CODEC_VOLUME || level > MAXIMUM_CODEC_VOLUME) {
        shell_warn(sh,
                   "Volume out of range (%d - %d)",
                   MINIMUM_CODEC_VOLUME,
                   MAXIMUM_CODEC_VOLUME);
    }

    player_status_t status = audio_player_set_volume(level);
    switch (status) {
    case PLAYER_OK:
        shell_print(sh, "Volume set to %d", level);
        break;

    case PLAYER_ERROR_CODEC_INIT:
        shell_error(sh, "Codec not initialized");
        break;

    case PLAYER_ERROR_CODEC_CFG:
        shell_error(sh, "Failed to configure codec volume");
        break;

    default:
        shell_error(sh, "Unknown error");
        break;
    }
    return (status == PLAYER_OK) ? 0 : -EPERM;
}

/**
 * @brief Enables mute on the audio output.
 */
static int cmd_audio_mute_true(const struct shell *sh, size_t argc, char **argv)
{
    player_status_t status = audio_player_set_mute(true);

    switch (status) {
    case PLAYER_OK:
        shell_print(sh, "Mute enabled");
        break;

    case PLAYER_ERROR_CODEC_INIT:
        shell_error(sh, "Codec not initialized");
        break;

    case PLAYER_ERROR_CODEC_CFG:
        shell_error(sh, "Failed to enable mute");
        break;

    default:
        shell_error(sh, "Unknown error while enabling mute");
        break;
    }
    return (status == PLAYER_OK) ? 0 : -EPERM;
}

/**
 * @brief Disables mute on the audio output.
 */
static int
cmd_audio_mute_false(const struct shell *sh, size_t argc, char **argv)
{
    player_status_t status = audio_player_set_mute(false);

    switch (status) {
    case PLAYER_OK:
        shell_print(sh, "Mute disabled");
        break;

    case PLAYER_ERROR_CODEC_INIT:
        shell_error(sh, "Codec not initialized");
        break;

    case PLAYER_ERROR_CODEC_CFG:
        shell_error(sh, "Failed to disable mute");
        break;

    default:
        shell_error(sh, "Unknown error while disabling mute");
        break;
    }
    return (status == PLAYER_OK) ? 0 : -EPERM;
}

/**
 * @brief Resets the audio codec by toggling the enable GPIO and reinitializing the codec.
 */
static int cmd_audio_reset(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "Resetting audio codec...");

    player_status_t status = codec_disable();
    if (status != PLAYER_OK) {
        shell_error(sh, "Failed to disable codec (code %d)", status);
        return -EIO;
    }

    k_sleep(K_MSEC(100)); // delay to ensure stable reset

    status = codec_enable();
    if (status != PLAYER_OK) {
        shell_error(sh, "Failed to enable codec (code %d)", status);
        return -EIO;
    }

    shell_print(sh, "Audio codec reset completed.");
    return 0;
}

/**
 * @brief Shell command to ping the audio thread and verify responsiveness.
 */
static int cmd_audio_ping(const struct shell *sh, size_t argc, char **argv)
{
    if (get_pause_status()) {
        shell_error(sh, "Ping is not available while playback is paused.");
        return -EINVAL;
    }
    shell_print(sh, "Starting audio thread ping...");

    size_t count = 4;
    for (size_t i = 0; i < count; i++) {
        uint64_t time_stamp = k_uptime_get();

        uint32_t result = audio_player_ping();

        int64_t delta_time = k_uptime_delta(&time_stamp);

        if (result & AUDIO_EVT_PING_STOP) {
            shell_print(
                    sh,
                    "Ping successful: audio thread is idle. Response time: %lld ms",
                    delta_time);
            break;
        }

        if (!(result & AUDIO_EVT_PING_REPLY)) {
            shell_error(sh, "Ping timeout: no reply from audio thread.");
            return -ETIMEDOUT;
        }

        shell_print(
                sh,
                "Ping successful: audio thread is running. Response time: %lld ms",
                delta_time);
        k_msleep(500);
    }
    return 0;
}

/**
 * @brief Display a list of audio files in the default audio directory.
 */
static int
cmd_audio_playlist_show(const struct shell *sh, size_t argc, char **argv)
{
    struct fs_dir_t  dir;
    struct fs_dirent entry;
    int              index = 1;

    fs_dir_t_init(&dir);
    if (fs_opendir(&dir, CONFIG_RPR_AUDIO_DEFAULT_PATH) != 0) {
        shell_error(sh, "Failed to open directory");
        return -ENOENT;
    }

    while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != 0) {
        if (entry.type == FS_DIR_ENTRY_FILE) {
            shell_print(sh, "%d: %s", index++, entry.name);
        }
    }

    fs_closedir(&dir);
    return 0;
}

/**
 * @brief Delete audio files from the default audio directory.
 * 
 * Usage: playlist delete <index|all>
 *
 * @param sh    Shell context.
 * @param argc  Argument count (expects at least 2).
 * @param argv  Argument values (expects "index" or "all").
 * @return 0 on success, -EINVAL for invalid usage, -ENOENT if file or folder not found.
 */
static int
cmd_audio_playlist_delete(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: playlist delete <index|all>");
        return -EINVAL;
    }

    struct fs_dir_t  dir;
    struct fs_dirent entry;
    char             full_path[FULL_AUDIO_PATH_MAX_LEN];
    int              index        = 1;
    int              target_index = -1;
    bool             delete_all   = false;
    bool             file_deleted = false;

    fs_dir_t_init(&dir);

    if (fs_opendir(&dir, CONFIG_RPR_AUDIO_DEFAULT_PATH) != 0) {
        shell_error(sh, "Failed to open directory");
        return -ENOENT;
    }

    if (strcmp(argv[1], "all") == 0) {
        delete_all = true;
    } else {
        target_index = atoi(argv[1]);
        if (target_index <= 0) {
            shell_error(sh, "Invalid index value");
            return -EINVAL;
        }
    }

    while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != 0) {
        if (entry.type == FS_DIR_ENTRY_FILE) {
            if (delete_all || index == target_index) {
                snprintf(full_path,
                         sizeof(full_path),
                         "%s/%s",
                         CONFIG_RPR_AUDIO_DEFAULT_PATH,
                         entry.name);
                fs_unlink(full_path);
                shell_print(sh, "Deleted: %s", entry.name);
                file_deleted = true;

                if (!delete_all) {
                    break;
                }
            }
            index++;
        }
    }

    fs_closedir(&dir);

    if (delete_all && file_deleted) {
        shell_print(sh, "All audio files deleted.");
    }

    if (!file_deleted) {
        shell_warn(sh, "File not found or nothing deleted.");
        return -ENOENT;
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(audio_playlist_cmds,
                               SHELL_CMD(show,
                                         NULL,
                                         "Show list of audio files",
                                         cmd_audio_playlist_show),
                               SHELL_CMD(delete,
                                         NULL,
                                         "Delete audio file by index or all",
                                         cmd_audio_playlist_delete),
                               SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
        audio_set,
        SHELL_CMD_ARG(volume, NULL, "Set volume level", cmd_audio_volume, 2, 0),
        SHELL_CMD(mute, NULL, "Enable mute", cmd_audio_mute_true),
        SHELL_CMD(unmute, NULL, "Disable mute", cmd_audio_mute_false),
        SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
        audio_cmds,
        SHELL_CMD(playlist, &audio_playlist_cmds, "Manage audio playlist", NULL),
        SHELL_CMD(play, NULL, "Play audio by index", cmd_audio_play),
        SHELL_CMD(stop, NULL, "Stop audio", cmd_audio_stop),
        SHELL_CMD(pause, NULL, "Pause audio", cmd_audio_pause),
        SHELL_CMD(info, NULL, "Show playback info", cmd_audio_info),
        SHELL_CMD(set, &audio_set, "Set volume/mute", NULL),
        SHELL_CMD(reset, NULL, "Reset the audio codec", cmd_audio_reset),
        SHELL_CMD(ping, NULL, "Ping audio playback thread", cmd_audio_ping),
        SHELL_SUBCMD_SET_END);

/**
 * @brief Turns on the specified LED.
 *
 * @param sh Shell context
 * @param argc Argument count
 * @param argv Argument vector: argv[1] = LED index (int)
 * @return 0 on success
 */
static int cmd_led_on(const struct shell *sh, size_t argc, char **argv)
{

    int          index      = atoi(argv[1]);
    led_status_t led_status = led_on(index);

    switch (led_status) {
    case LED_INVALID_INDEX:
        shell_warn(sh, "Wrong LED index");
        break;
    case LED_FAILED:
        shell_error(sh, "LED operation failed");
        break;
    case LED_OK:
        shell_print(sh, "LED %d ON", index);
        break;
    default:
        shell_error(sh, "Unknown LED status");
        break;
    }

    return 0;
}

/**
 * @brief Turns off the specified LED.
 *
 * @param sh Shell context
 * @param argc Argument count
 * @param argv Argument vector: argv[1] = LED index (int)
 * @return 0 on success
 */
static int cmd_led_off(const struct shell *sh, size_t argc, char **argv)
{

    int          index      = atoi(argv[1]);
    led_status_t led_status = led_off(index);

    switch (led_status) {
    case LED_INVALID_INDEX:
        shell_warn(sh, "Wrong LED index");
        break;
    case LED_FAILED:
        shell_error(sh, "LED operation failed");
        break;
    case LED_OK:
        shell_print(sh, "LED %d OFF", index);
        break;
    default:
        shell_error(sh, "Unknown LED status");
        break;
    }

    return 0;
}

/**
 * @brief Blinks the specified LED with given on/off durations.
 *
 * @param sh Shell context
 * @param argc Argument count
 * @param argv Argument vector: 
 *             argv[1] = LED index (int), 
 *             argv[2] = ON time (ms), 
 *             argv[3] = OFF time (ms)
 * @return 0 on success or -EINVAL for invalid params
 */
static int cmd_led_blink(const struct shell *sh, size_t argc, char **argv)
{

    int index  = atoi(argv[1]);
    int on_ms  = atoi(argv[2]);
    int off_ms = atoi(argv[3]);

    if (on_ms <= 0 || off_ms <= 0) {
        shell_error(sh, "Invalid time values");
        return -EINVAL;
    }

    led_status_t led_status = led_blink(index, on_ms, off_ms);

    switch (led_status) {
    case LED_INVALID_INDEX:
        shell_warn(sh, "Wrong LED index");
        break;
    case LED_FAILED:
        shell_error(sh, "LED operation failed");
        break;
    case LED_OK:
        shell_print(sh,
                    "LED %d set to blink (ON=%dms, OFF=%dms)",
                    index,
                    on_ms,
                    off_ms);
        break;
    default:
        shell_error(sh, "Unknown LED status");
        break;
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
        sub_led,
        SHELL_CMD_ARG(on,
                      NULL,
                      "Turn LED on. Usage: led on <index>",
                      cmd_led_on,
                      2,
                      0),
        SHELL_CMD_ARG(off,
                      NULL,
                      "Turn LED off. Usage: led off <index>",
                      cmd_led_off,
                      2,
                      0),
        SHELL_CMD_ARG(blink,
                      NULL,
                      "Blink LED. Usage: led blink <index> <on_ms> <off_ms>",
                      cmd_led_blink,
                      4,
                      0),
        SHELL_SUBCMD_SET_END);

/**
 * @brief Enables the watchdog timer.
 */
static int cmd_watchdog_enable(const struct shell *sh, size_t argc, char **argv)
{

    if (watchdog_enable() != 0) {
        shell_error(sh, "Failed to enable watchdog");
        return -1;
    }
    shell_print(sh, "Watchdog enable on");
    return 0;
}

/**
 * @brief Disables the watchdog timer.
 */
static int
cmd_watchdog_disable(const struct shell *sh, size_t argc, char **argv)
{
    if (watchdog_disable() != 0) {
        shell_error(sh, "Failed to disable watchdog");
        return -1;
    }
    shell_print(sh, "Watchdog disable off");
    return 0;
}

/**
 * @brief Feeds (resets) the watchdog to prevent reset.
 */
static int cmd_watchdog_feed(const struct shell *sh, size_t argc, char **argv)
{
    if (watchdog_feed() != 0) {
        shell_error(sh, "Watchdog feed failed at iteration");
    } else {
        shell_print(sh, "Watchdog fed");
    }

    return 0;
}

/**
 * @brief Demonstrates watchdog feeding.
 * 
 * Enables the watchdog, simulates some work with periodic feeding,
 * then disables the watchdog.
 */
static int
demo_watchdog_feeding(const struct shell *sh, size_t argc, char **argv)
{

#ifdef CONFIG_EXAMPLES_ENABLE_MAIN_EXAMPLES
    shell_print(
            sh,
            "Watchdog feeding is already handled by the power supervisor example.");
    shell_print(
            sh,
            "To run this demo manually, disable CONFIG_EXAMPLES_ENABLE_MAIN_EXAMPLES.");
    return 0;
#endif

    shell_print(sh, "[DEMO] Starting watchdog feeding demonstration...");

    if (watchdog_enable() != 0) {
        shell_error(sh, "Failed to enable watchdog");
        return -1;
    }

    for (int i = 0; i < 10; i++) {
        shell_print(sh, "Doing some work, iteration %d...", i + 1);
        k_sleep(K_MSEC(500)); // simulate work
        watchdog_feed();
        shell_print(sh, "Watchdog fed");
    }

    if (watchdog_disable() != 0) {
        shell_warn(sh, "Watchdog disable failed");
    } else {
        shell_print(sh, "Watchdog feeding demo complete and watchdog disabled");
    }

    return 0;
}

#ifdef CONFIG_EXAMPLES_ENABLE_MAIN_EXAMPLES
/**
 * @brief Simulates a system freeze for watchdog testing purposes.
 *
 * This function enters an infinite sleep to intentionally block execution.
 * It is used to test the system watchdog's ability to detect unresponsive behavior.
 *
 * @return false Always returns false (unreachable).
 */
static bool watchdog_freeze_func(void)
{
    while (true) {
        k_sleep(K_FOREVER); // simulate freeze
    }
    return false;
}
#endif

/**
 * @brief Demonstrates watchdog triggering by freezing the system.
 * 
 * Enables the watchdog, simulates some work, and then stops feeding,
 * causing a system reset.
 */
static int
demo_watchdog_freeze(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "[DEMO] Starting watchdog freeze demonstration...");

    if (watchdog_enable() != 0) {
        shell_error(sh, "Failed to enable watchdog");
        return -1;
    }

#ifdef CONFIG_EXAMPLES_ENABLE_MAIN_EXAMPLES
    shell_warn(sh, "Power supervisor example is active.");
    shell_warn(sh, "Registering simulated freeze callback for supervisor...");
    k_sleep(K_MSEC(1500));
    supervisor_ping_register_callback(watchdog_freeze_func);
    return 0;
#endif

    for (int i = 0; i < 5; i++) {
        shell_print(sh, "Doing some work, iteration %d...", i + 1);
        k_sleep(K_MSEC(500)); // simulate work
        watchdog_feed();
        shell_print(sh, "Watchdog fed");
    }

    shell_warn(sh, "System will now freeze without feeding the watchdog!");
    shell_warn(sh, "Waiting for reset...");

    while (true) {
        k_sleep(K_FOREVER); // simulate freeze
    }
    watchdog_disable();
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
        sub_demo_watchdog,
        SHELL_CMD(feeding,
                  NULL,
                  "Demo: Feeding the watchdog while doing work",
                  demo_watchdog_feeding),
        SHELL_CMD(freeze,
                  NULL,
                  "Demo: Freeze system and trigger watchdog reset",
                  demo_watchdog_freeze),
        SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
        sub_watchdog,
        SHELL_CMD(enable, NULL, "Enable watchdog", cmd_watchdog_enable),
        SHELL_CMD(disable, NULL, "Disable watchdog", cmd_watchdog_disable),
        SHELL_CMD(feed, NULL, "Feed watchdog", cmd_watchdog_feed),
        SHELL_CMD(demo, &sub_demo_watchdog, "Watchdog demonstration", NULL),
        SHELL_SUBCMD_SET_END);

/**
 * @brief Checks if sound is currently detected by the microphone.
 */
static int cmd_microphone(const struct shell *sh, size_t argc, char **argv)
{
    bool sound = microphone_is_sound_detected();
    shell_print(sh, "Sound detected: %s", sound ? "YES" : "NO");
    return 0;
}

/**
 * @brief Enables charger output.
 */
static int cmd_charger_enable(const struct shell *sh, size_t argc, char **argv)
{
    if (charger_enable() != 0) {
        shell_error(sh, "Failed to enable charger");
        return -1;
    } else
        shell_print(sh, "Charger enable on");
    return 0;
}

/**
 * @brief Disables charger output.
 */
static int cmd_charger_disable(const struct shell *sh, size_t argc, char **argv)
{
    if (charger_disable() != 0) {
        shell_error(sh, "Failed to disable charger");
        return -1;
    } else
        shell_print(sh, "Charger enable off");

    return 0;
}

/**
 * @brief Gets charger status, input power status and charger enable state.
 */
static int
cmd_charger_get_status(const struct shell *sh, size_t argc, char **argv)
{
    charger_status_t     charger_status     = charger_get_status();
    input_power_status_t power_status       = input_power_get_status();
    input_power_status_t detected_status    = is_input_power_detected();
    bool                 is_charger_enabled = charger_get_state();

    // Charger enable state
    shell_print(sh, "Charger enabled: %s", is_charger_enabled ? "YES" : "NO");

    // Input power detection (custom GPIO)
    if (detected_status == INPUT_POWER_FAILED) {
        shell_error(sh, "Failed to detect input power (detect pin error)");
    } else {
        shell_print(sh,
                    "Input power detected: %s",
                    detected_status == INPUT_POWER_DETECTED ? "YES" : "NO");
    }

    if (detected_status == INPUT_POWER_NOT_DETECTED) {
        return 0;
    }

    // Charger charging status
    if (charger_status == CHARGER_STATUS_FAILED) {
        shell_error(sh, "Failed to read charger status (CHGOK pin error)");
    } else {
        shell_print(sh,
                    "Charging status (CHGOK): %s",
                    charger_status == CHARGER_STATUS_CHARGING ? "CHARGING" :
                                                                "DONE/FAULT");
    }

    // Input power validity (ACOK pin)
    if (power_status == INPUT_POWER_FAILED) {
        shell_error(sh, "Failed to read input power status (ACOK pin error)");
    } else {
        shell_print(sh,
                    "Input power (ACOK): %s",
                    power_status == INPUT_POWER_VALID ? "POWER VALID" :
                                                        "POWER NOT VALID");
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
        sub_charger,
        SHELL_CMD(enable, NULL, "Enable charger", cmd_charger_enable),
        SHELL_CMD(disable, NULL, "Disable charger", cmd_charger_disable),
        SHELL_CMD(status, NULL, "Get charger status", cmd_charger_get_status),
        SHELL_SUBCMD_SET_END);

/**
 * @brief Reads current battery voltage and estimates charge level.
 */
static int
cmd_battery_get_status(const struct shell *sh, size_t argc, char **argv)
{

    int voltage = -1;

    if (charger_get_state()) {
        if (charger_disable() != 0) {
            shell_error(sh, "Failed to disable charger");
        }
        k_msleep(100);

        voltage = battery_sample();

        if (charger_enable() != 0) {
            shell_error(sh, "Failed to enable charger");
        }
    } else {
        voltage = battery_sample();
    }

    if (voltage < 0) {
        shell_error(sh, "Failed to read battery voltage (err %d)", voltage);
    } else {
        unsigned int level = battery_level_pptt(voltage);
        shell_print(sh,
                    "Voltage is %d mV, battery level ~%u%%",
                    voltage,
                    level / 100);
    }
    return 0;
}

/**
 * @brief CLI command to show if modem is the default interface.
 * (only if CONFIG_RPR_MODEM is enabled)
 */
static int
cmd_modem_get_default(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODEM

    if (is_modem_iface_default()) {
        shell_print(sh, "Modem is set as the default network interface.");
    } else {
        shell_print(sh, "Modem is NOT the default network interface.");
    }
#else
    shell_info(sh, "Set CONFIG_RPR_MODEM to enable c16qs support.");
#endif
    return 0;
}

/**
 * @brief Set modem interface as the default network interface.
 * (only if CONFIG_RPR_MODEM is enabled)
 * 
 */
static int
cmd_modem_set_default(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODEM
    shell_print(sh, "Setting modem as default interface...");

    c16qs_modem_status_t status = modem_set_iface_default();

    switch (status) {
    case MODEM_SUCCESS:
        shell_print(sh, "Modem interface set as default and connected.");
        break;
    case MODEM_ERR_NO_INTERFACE:
        shell_error(sh, "Modem interface is not found.");
        break;
    default:
        shell_error(sh, "Unknown modem error.");
        break;
    }
#else
    shell_info(sh, "Set CONFIG_RPR_MODEM to enable c16qs support.");

#endif
    return 0;
}

/**
 * @brief Starts modem (only if CONFIG_RPR_MODEM is enabled).
 */
static int cmd_modem_init(const struct shell *sh, size_t argc, char **argv)
{

#ifdef CONFIG_RPR_MODEM

    shell_print(sh, "Starts modem...");

    c16qs_modem_status_t status = modem_init_and_connect();
    switch (status) {
    case MODEM_SUCCESS:
        shell_print(sh, "Modem successfully started. Connection request sent.");
        break;
    case MODEM_ERR_INIT_FAILED:
        shell_error(sh, "Modem initialization failed.");
        break;
    case MODEM_ERR_IFACE_UP:
        shell_error(sh, "Failed to bring network interface up.");
        break;
    default:
        shell_error(sh, "Unknown modem error.");
        break;
    }

#else
    shell_info(sh, "Set CONFIG_RPR_MODEM to enable c16qs support.");

#endif
    return 0;
}

/**
 * @brief Resets the modem (only if CONFIG_RPR_MODEM is enabled).
 */
static int cmd_modem_reset(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODEM
    c16qs_modem_status_t status = modem_reset();
    switch (status) {
    case MODEM_SUCCESS:
        shell_print(sh, "Reset request successfully sent.");
        break;
    case MODEM_ERR_POWER_DOWN:
        shell_error(sh, "Failed to power down modem during reset.");
        break;
    case MODEM_ERR_INIT_FAILED:
        shell_error(sh, "Modem initialization failed after reset.");
        break;
    case MODEM_ERR_IFACE_UP:
        shell_error(sh, "ifece modem error.");
        break;
    default:
        shell_error(sh, "Unknown modem error.");
        break;
    }
#else
    shell_info(sh, "Set CONFIG_RPR_MODEM to enable c16qs support.");

#endif
    return 0;
}

/**
 * @brief Powers down the modem (only if CONFIG_RPR_MODEM is enabled).
 */
static int cmd_modem_poweroff(const struct shell *sh, size_t argc, char **argv)
{

#ifdef CONFIG_RPR_MODEM
    c16qs_modem_status_t status = modem_shutdown();
    switch (status) {
    case MODEM_SUCCESS:
        shell_print(sh, "Modem shutdown successfully.");
        break;
    case MODEM_ERR_IFACE_DOWN:
        shell_error(sh, "Failed to bring network interface down.");
        break;
    case MODEM_ERR_POWER_DOWN:
        shell_error(sh, "Failed to power down modem.");
        break;
    default:
        shell_error(sh, "Unknown modem error.");
        break;
    }

#else
    shell_info(sh, "Set CONFIG_RPR_MODEM to enable c16qs support.");

#endif
    return 0;
}

/**
 * @brief Show modem status (only if CONFIG_RPR_MODEM is enabled).
 */
static int cmd_modem_status(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODEM
    bool initialized   = is_modem_initialized();
    bool connected     = is_modem_connected();
    bool iface_default = is_modem_iface_default();

    shell_print(sh, "Modem initialized: %s", initialized ? "Yes" : "No");
    shell_print(sh, "Modem connected: %s", connected ? "Yes" : "No");
    shell_print(
            sh, "Modem interface default: %s", iface_default ? "Yes" : "No");

#else
    shell_info(sh, "Set CONFIG_RPR_MODEM to enable c16qs support.");

#endif
    return 0;
}

/**
 * @brief Show modem info (only if CONFIG_RPR_MODEM is enabled).
 */
static int cmd_modem_info(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODEM
    modem_info_t         info;
    c16qs_modem_status_t ret = c16qs_get_cellular_info(&info);
    switch (ret) {
    case MODEM_ERR_NOT_INIT:
        shell_error(sh, "Modem not initialized");
        break;
    case MODEM_ERR_INFO_RETRIEVAL:
        shell_error(sh, "Failed to retrieve modem info");
        break;
    case MODEM_SUCCESS:
        shell_print(sh,
                    "IMEI: %s",
                    strlen(info.imei) != 0 ? info.imei : "Updating...");
        shell_print(sh,
                    "MODEL_ID: %s",
                    strlen(info.model_id) != 0 ? info.model_id : "Updating...");
        shell_print(sh,
                    "MANUFACTURER: %s",
                    strlen(info.manufacturer) != 0 ? info.manufacturer :
                                                     "Updating...");
        shell_print(sh,
                    "SIM_IMSI: %s",
                    strlen(info.sim_imsi) != 0 ? info.sim_imsi : "Updating...");
        shell_print(sh,
                    "SIM_ICCID: %s",
                    strlen(info.sim_iccid) != 0 ? info.sim_iccid :
                                                  "Updating...");
        shell_print(sh,
                    "FW_VERSION: %s",
                    strlen(info.fw_version) != 0 ? info.fw_version :
                                                   "Updating...");
        break;
    default:
        shell_error(sh, "Unknown error");
        break;
    }
#else
    shell_info(sh, "Set CONFIG_RPR_MODEM to enable c16qs support.");
#endif
    return 0;
}

/**
 * @brief Show modem RSSI (only if CONFIG_RPR_MODEM is enabled).
 */
static int cmd_modem_rssi(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODEM

    int16_t              rssi;
    c16qs_modem_status_t ret = c16qs_get_signal_rssi(&rssi);
    switch (ret) {
    case MODEM_ERR_NOT_INIT:
        shell_error(sh, "Modem not initialized");
        break;
    case MODEM_ERR_INFO_RETRIEVAL:
        shell_error(sh, "Failed to retrieve RSSI");
        break;
    case MODEM_SUCCESS:
        shell_print(sh, "RSSI: %d", rssi);
        break;
    default:
        shell_error(sh, "Unknown error");
        break;
    }
#else
    shell_info(sh, "Set CONFIG_RPR_MODEM to enable c16qs support.");
#endif
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
        cmd_modem_default,
        SHELL_CMD(get,
                  NULL,
                  "Check if modem is the default interface",
                  cmd_modem_get_default),
        SHELL_CMD(set,
                  NULL,
                  "Set modem as the default interface",
                  cmd_modem_set_default),
        SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
        sub_modem,
        SHELL_CMD(init, NULL, "Start modem", cmd_modem_init),
        SHELL_CMD(reset, NULL, "Reset modem", cmd_modem_reset),
        SHELL_CMD(poweroff, NULL, "Power down modem", cmd_modem_poweroff),
        SHELL_CMD(status, NULL, "Check modem status", cmd_modem_status),
        SHELL_CMD(info, NULL, "Get modem info", cmd_modem_info),
        SHELL_CMD(rssi, NULL, "Get RSSI value", cmd_modem_rssi),
        SHELL_CMD(default, &cmd_modem_default, "Modem interface control", NULL),
        SHELL_SUBCMD_SET_END);

#define HTTP_GET_RESPONSE_BUF_LEN 512

/**
 * @brief CLI command handler for performing an HTTP GET request.
 * CONFIG_RPR_MODULE_HTTP must be enabled
 */
static int cmd_http_get(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODULE_HTTP

    if (argc < 2) {
        shell_error(sh, "Usage: http get <url>");
        return -EINVAL;
    }

    const char *url                                     = argv[1];
    uint16_t    http_status_code                        = 0;
    char        response_buf[HTTP_GET_RESPONSE_BUF_LEN] = { 0 };
    ;

    struct get_context get_ctx = {
        .response_buffer = response_buf,
        .buffer_capacity = sizeof(response_buf),
        .response_len    = 0,
    };

    shell_print(sh, "Sending GET request to: %s", url);

    http_status_t ret = http_get_request(url, &get_ctx, &http_status_code);

    if (ret == HTTP_CLIENT_OK) {
        shell_print(sh, "HTTP %d OK", http_status_code);
        shell_print(sh, "GET: %s", response_buf);
    } else {
        shell_error(sh, "GET failed: ret=%d", ret);
    }

    return ret;

#else
    shell_info(sh, "Set CONFIG_RPR_MODULE_HTTP to enable http support.");
#endif
    return 0;
}

/**
 * @brief CLI command handler for performing an HTTP POST request.
 * CONFIG_RPR_MODULE_HTTP must be enabled.
 */
static int cmd_http_post(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODULE_HTTP

    if (argc < 3) {
        shell_error(sh, "Usage: http post <url> <payload>");
        return -EINVAL;
    }

    const char *url     = argv[1];
    const char *payload = argv[2];

    uint16_t http_status_code                        = 0;
    char     response_buf[HTTP_GET_RESPONSE_BUF_LEN] = { 0 };

    const char *post_headers[] = { "Content-Type: application/json\r\n", NULL };

    struct post_context post_ctx = {
        .response = {
            .response_buffer = response_buf,
            .buffer_capacity = sizeof(response_buf),
            .response_len    = 0,
        },
        .payload     = payload,
        .payload_len = strlen(payload),
        .headers     = post_headers,
        
    };

    shell_print(sh, "Sending POST request to: %s", url);

    http_status_t ret = http_post_request(url, &post_ctx, &http_status_code);

    if (ret == HTTP_CLIENT_OK) {
        shell_print(sh, "HTTP %d OK", http_status_code);
        shell_print(sh, "POST response: %s", response_buf);
    } else {
        shell_error(sh, "POST failed: ret=%d", ret);
    }
    return ret;

#else
    shell_info(sh, "Set CONFIG_RPR_MODULE_HTTP to enable http support.");
#endif
    return 0;
}

/**
 * @brief CLI command handler for downloading a file via HTTP/HTTPS.
 * CONFIG_RPR_MODULE_HTTP must be enabled.
 */
static int cmd_http_download(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODULE_HTTP

    if (argc < 2) {
        shell_print(sh, "Usage: http download audio <url>");
        return -EINVAL;
    }

    const char *url              = argv[1];
    uint16_t    http_status_code = 0;

    shell_print(sh, "Downloading audio from: %s", url);
    int ret = http_download_file_request(
            url, CONFIG_RPR_AUDIO_DEFAULT_PATH, &http_status_code);

    if (ret == 0) {
        shell_print(sh,
                    "Download audio successful. Status code: %d",
                    http_status_code);
    } else {
        shell_error(sh, "Download failed. Error code: %d", ret);
    }
    return ret;

#else
    shell_info(sh, "Set CONFIG_RPR_MODULE_HTTP to enable http support.");
#endif
    return 0;
}

/**
 * @brief CLI command handler for downloading and flashing a firmware update via HTTP/HTTPS.
 */
static int
cmd_http_download_update(const struct shell *sh, size_t argc, char **argv)
{
#if defined(CONFIG_RPR_MODULE_DFU) && defined(CONFIG_RPR_MODULE_HTTP)

    if (argc < 2) {
        shell_print(sh, "Usage: http download update <url>");
        return -EINVAL;
    }

    const char *url              = argv[1];
    uint16_t    http_status_code = 0;

    shell_print(sh, "Downloading update from: %s", url);
    int ret = http_download_update_request(url, &http_status_code);

    if (ret == 0) {
        shell_print(sh,
                    "Firmware update downloaded successfully. HTTP status: %d",
                    http_status_code);
    } else {
        shell_error(sh, "Update download failed. Error code: %d", ret);
    }

    return ret;

#else
    shell_info(sh,
               "Set CONFIG_RPR_MODULE_HTTP to enable http support.\n"
               "Set CONFIG_RPR_MODULE_DFU to enable DFU support.");

#endif
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
        sub_http_download,
        SHELL_CMD(
                audio,
                NULL,
                "Download audio file via HTTP. Usage: http download audio <url>",
                cmd_http_download),
        SHELL_CMD(
                update,
                NULL,
                "Download firmware update via HTTP. Usage: http download update <url>",
                cmd_http_download_update),
        SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
        sub_http,
        SHELL_CMD(download,
                  &sub_http_download,
                  "Download files via HTTP (audio, update)",
                  NULL),
        SHELL_CMD(get,
                  NULL,
                  "GET request and print response. Usage: http get <url>",
                  cmd_http_get),
        SHELL_CMD(post,
                  NULL,
                  "POST request. Usage: http post <url> <payload>",
                  cmd_http_post),
        SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
        sub_internet,
        SHELL_CMD(ethernet, &sub_eth, "Ethernet interface management", NULL),
        SHELL_CMD(wifi, &wifi_cmds, "Wi-Fi interface commands", NULL),
        SHELL_CMD(modem, &sub_modem, "Modem control", NULL),
        SHELL_CMD(http, &sub_http, "HTTP client commands", NULL),
        SHELL_SUBCMD_SET_END);

/**
 * @brief MQTT init command
 */
static int cmd_mqtt_init(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODULE_MQTT
    mqtt_status_t ret = mqtt_init();
    
    switch (ret) {
    case MQTT_SUCCESS:
        shell_print(sh, "MQTT module initialized successfully");
        break;
    default:
        shell_error(sh, "Failed to initialize MQTT module: %d", ret);
        break;
    }
    
    return ret;
#else
    shell_info(sh, "Set CONFIG_RPR_MODULE_MQTT to enable MQTT support.");
    return 0;
#endif
}

/**
 * @brief MQTT connect command
 */
static int cmd_mqtt_connect(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODULE_MQTT
    mqtt_status_t ret = mqtt_module_connect();
    
    switch (ret) {
    case MQTT_SUCCESS:
        shell_print(sh, "MQTT connection initiated successfully");
        break;
    case MQTT_ERR_NOT_INITIALIZED:
        shell_error(sh, "MQTT module not initialized");
        break;
    case MQTT_ERR_CONNECTION_FAILED:
        shell_error(sh, "Failed to connect to MQTT broker");
        break;
    default:
        shell_error(sh, "Unknown MQTT error: %d", ret);
        break;
    }
    
    return ret;
#else
    shell_info(sh, "Set CONFIG_RPR_MODULE_MQTT to enable MQTT support.");
    return 0;
#endif
}

/**
 * @brief MQTT disconnect command
 */
static int cmd_mqtt_disconnect(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODULE_MQTT
    mqtt_status_t ret = mqtt_module_disconnect();
    
    switch (ret) {
    case MQTT_SUCCESS:
        shell_print(sh, "MQTT disconnected successfully");
        break;
    case MQTT_ERR_NOT_INITIALIZED:
        shell_error(sh, "MQTT module not initialized");
        break;
    default:
        shell_error(sh, "Unknown MQTT error: %d", ret);
        break;
    }
    
    return ret;
#else
    shell_info(sh, "Set CONFIG_RPR_MODULE_MQTT to enable MQTT support.");
    return 0;
#endif
}

/**
 * @brief MQTT status command
 */
static int cmd_mqtt_status(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODULE_MQTT
    bool connected = mqtt_is_connected();
    bool auto_reconnect = mqtt_is_auto_reconnect_enabled();
    
    shell_print(sh, "MQTT Status:");
    shell_print(sh, "  Connected: %s", connected ? "Yes" : "No");
    shell_print(sh, "  Auto-reconnect: %s", auto_reconnect ? "Enabled" : "Disabled");
    shell_print(sh, "  Broker: %s:%d", CONFIG_RPR_MQTT_BROKER_HOST, CONFIG_RPR_MQTT_BROKER_PORT);
    shell_print(sh, "  Client ID: %s", CONFIG_RPR_MQTT_CLIENT_ID);
    shell_print(sh, "  Heartbeat Topic: %s", CONFIG_RPR_MQTT_HEARTBEAT_TOPIC);
    shell_print(sh, "  Heartbeat Interval: %d seconds", CONFIG_RPR_MQTT_HEARTBEAT_INTERVAL_SEC);
    return 0;
#else
    shell_info(sh, "Set CONFIG_RPR_MODULE_MQTT to enable MQTT support.");
    return 0;
#endif
}

/**
 * @brief MQTT publish command
 */
static int cmd_mqtt_publish(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODULE_MQTT
    if (argc < 3) {
        shell_error(sh, "Usage: mqtt publish <topic> <message>");
        return -EINVAL;
    }
    
    const char *topic = argv[1];
    const char *message = argv[2];
    
    mqtt_status_t ret = mqtt_module_publish(topic, message, strlen(message));
    
    switch (ret) {
    case MQTT_SUCCESS:
        shell_print(sh, "Message published to '%s'", topic);
        break;
    case MQTT_ERR_NOT_INITIALIZED:
        shell_error(sh, "MQTT module not initialized");
        break;
    case MQTT_ERR_CONNECTION_FAILED:
        shell_error(sh, "MQTT not connected");
        break;
    case MQTT_ERR_PUBLISH_FAILED:
        shell_error(sh, "Failed to publish message");
        break;
    case MQTT_ERR_INVALID_PARAM:
        shell_error(sh, "Invalid parameters");
        break;
    default:
        shell_error(sh, "Unknown MQTT error: %d", ret);
        break;
    }
    
    return ret;
#else
    shell_info(sh, "Set CONFIG_RPR_MODULE_MQTT to enable MQTT support.");
    return 0;
#endif
}

/**
 * @brief MQTT heartbeat start command
 */
static int cmd_mqtt_heartbeat_start(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODULE_MQTT
    mqtt_status_t ret = mqtt_start_heartbeat();
    
    switch (ret) {
    case MQTT_SUCCESS:
        shell_print(sh, "MQTT heartbeat started");
        break;
    case MQTT_ERR_NOT_INITIALIZED:
        shell_error(sh, "MQTT module not initialized");
        break;
    default:
        shell_error(sh, "Failed to start heartbeat: %d", ret);
        break;
    }
    
    return ret;
#else
    shell_info(sh, "Set CONFIG_RPR_MODULE_MQTT to enable MQTT support.");
    return 0;
#endif
}

/**
 * @brief MQTT heartbeat stop command
 */
static int cmd_mqtt_heartbeat_stop(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODULE_MQTT
    mqtt_status_t ret = mqtt_stop_heartbeat();
    
    switch (ret) {
    case MQTT_SUCCESS:
        shell_print(sh, "MQTT heartbeat stopped");
        break;
    default:
        shell_error(sh, "Failed to stop heartbeat: %d", ret);
        break;
    }
    
    return ret;
#else
    shell_info(sh, "Set CONFIG_RPR_MODULE_MQTT to enable MQTT support.");
    return 0;
#endif
}

/**
 * @brief MQTT manual heartbeat command
 */
static int cmd_mqtt_heartbeat_send(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODULE_MQTT
    mqtt_status_t ret = mqtt_send_heartbeat();
    
    switch (ret) {
    case MQTT_SUCCESS:
        shell_print(sh, "Heartbeat message sent");
        break;
    case MQTT_ERR_CONNECTION_FAILED:
        shell_error(sh, "MQTT not connected");
        break;
    case MQTT_ERR_PUBLISH_FAILED:
        shell_error(sh, "Failed to send heartbeat");
        break;
    default:
        shell_error(sh, "Failed to send heartbeat: %d", ret);
        break;
    }
    
    return ret;
#else
    shell_info(sh, "Set CONFIG_RPR_MODULE_MQTT to enable MQTT support.");
    return 0;
#endif
}

/**
 * @brief Test network connectivity to MQTT broker
 */
static int cmd_mqtt_test_connection(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODULE_MQTT
    shell_print(sh, "Testing network connectivity to MQTT broker...");
    shell_print(sh, "Broker: %s:%d", CONFIG_RPR_MQTT_BROKER_HOST, CONFIG_RPR_MQTT_BROKER_PORT);
    
    /* Test basic network connectivity using socket */
    int sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        shell_error(sh, "Failed to create socket: %d", errno);
        return -1;
    }
    
    struct sockaddr_in broker_addr;
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(CONFIG_RPR_MQTT_BROKER_PORT);
    
    int ret = zsock_inet_pton(AF_INET, CONFIG_RPR_MQTT_BROKER_HOST, &broker_addr.sin_addr);
    if (ret != 1) {
        shell_error(sh, "Invalid broker IP address: %s", CONFIG_RPR_MQTT_BROKER_HOST);
        zsock_close(sock);
        return -1;
    }
    
    shell_print(sh, "Attempting to connect to %s:%d...", 
                CONFIG_RPR_MQTT_BROKER_HOST, CONFIG_RPR_MQTT_BROKER_PORT);
    
    ret = zsock_connect(sock, (struct sockaddr *)&broker_addr, sizeof(broker_addr));
    if (ret < 0) {
        shell_error(sh, "Failed to connect to broker: %d (errno: %d)", ret, errno);
        shell_print(sh, "Possible causes:");
        shell_print(sh, "  - Network interface not up or not default");
        shell_print(sh, "  - No IP address assigned");
        shell_print(sh, "  - Broker not reachable");
        shell_print(sh, "  - Firewall blocking connection");
    } else {
        shell_print(sh, "Successfully connected to MQTT broker!");
        shell_print(sh, "Network connectivity is working.");
        
        /* Test sending a simple message to see if connection stays alive */
        const char test_msg[] = "\x10\x0C\x00\x04MQTT\x04\x00\x00\x0A\x00\x00"; // MQTT CONNECT packet
        int sent = zsock_send(sock, test_msg, sizeof(test_msg)-1, 0);
        if (sent > 0) {
            shell_print(sh, "Sent test MQTT packet (%d bytes)", sent);
            
            /* Try to receive response */
            char response[64];
            int received = zsock_recv(sock, response, sizeof(response), MSG_DONTWAIT);
            if (received > 0) {
                shell_print(sh, "Received response (%d bytes)", received);
            } else {
                shell_print(sh, "No immediate response (normal for test packet)");
            }
        } else {
            shell_error(sh, "Failed to send test packet: %d", sent);
        }
    }
    
    zsock_close(sock);
    return ret;
#else
    shell_info(sh, "Set CONFIG_RPR_MODULE_MQTT to enable MQTT support.");
    return 0;
#endif
}

/**
 * @brief Enable MQTT auto-reconnection
 */
static int cmd_mqtt_auto_reconnect_enable(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODULE_MQTT
    mqtt_status_t ret = mqtt_enable_auto_reconnect();
    
    switch (ret) {
    case MQTT_SUCCESS:
        shell_print(sh, "MQTT auto-reconnection enabled");
        break;
    default:
        shell_error(sh, "Failed to enable auto-reconnection: %d", ret);
        break;
    }
    
    return ret;
#else
    shell_info(sh, "Set CONFIG_RPR_MODULE_MQTT to enable MQTT support.");
    return 0;
#endif
}

/**
 * @brief Disable MQTT auto-reconnection
 */
static int cmd_mqtt_auto_reconnect_disable(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODULE_MQTT
    mqtt_status_t ret = mqtt_disable_auto_reconnect();
    
    switch (ret) {
    case MQTT_SUCCESS:
        shell_print(sh, "MQTT auto-reconnection disabled");
        break;
    default:
        shell_error(sh, "Failed to disable auto-reconnection: %d", ret);
        break;
    }
    
    return ret;
#else
    shell_info(sh, "Set CONFIG_RPR_MODULE_MQTT to enable MQTT support.");
    return 0;
#endif
}

/**
 * @brief Show MQTT auto-reconnection status
 */
static int cmd_mqtt_auto_reconnect_status(const struct shell *sh, size_t argc, char **argv)
{
#ifdef CONFIG_RPR_MODULE_MQTT
    bool enabled = mqtt_is_auto_reconnect_enabled();
    shell_print(sh, "MQTT auto-reconnection: %s", enabled ? "Enabled" : "Disabled");
    return 0;
#else
    shell_info(sh, "Set CONFIG_RPR_MODULE_MQTT to enable MQTT support.");
    return 0;
#endif
}

/**
 * @brief Shell command to control MQTT shell backend
 */
/* MQTT shell backend is handled by Zephyr built-in backend when enabled via Kconfig */

#ifdef CONFIG_RPR_MODULE_MQTT
SHELL_STATIC_SUBCMD_SET_CREATE(
        sub_mqtt_heartbeat,
        SHELL_CMD(start, NULL, "Start periodic heartbeat", cmd_mqtt_heartbeat_start),
        SHELL_CMD(stop, NULL, "Stop periodic heartbeat", cmd_mqtt_heartbeat_stop),
        SHELL_CMD(send, NULL, "Send heartbeat now", cmd_mqtt_heartbeat_send),
        SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
        sub_mqtt_auto_reconnect,
        SHELL_CMD(enable, NULL, "Enable auto-reconnection", cmd_mqtt_auto_reconnect_enable),
        SHELL_CMD(disable, NULL, "Disable auto-reconnection", cmd_mqtt_auto_reconnect_disable),
        SHELL_CMD(status, NULL, "Show auto-reconnection status", cmd_mqtt_auto_reconnect_status),
        SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
        sub_mqtt,
        SHELL_CMD(init, NULL, "Initialize MQTT module", cmd_mqtt_init),
        SHELL_CMD(connect, NULL, "Connect to MQTT broker", cmd_mqtt_connect),
        SHELL_CMD(disconnect, NULL, "Disconnect from MQTT broker", cmd_mqtt_disconnect),
        SHELL_CMD(status, NULL, "Show MQTT status", cmd_mqtt_status),
        SHELL_CMD_ARG(publish, NULL, "Publish message. Usage: mqtt publish <topic> <message>", cmd_mqtt_publish, 3, 0),
        SHELL_CMD(heartbeat, &sub_mqtt_heartbeat, "Heartbeat control", NULL),
        SHELL_CMD(reconnect, &sub_mqtt_auto_reconnect, "Auto-reconnection control", NULL),
        SHELL_CMD(test, NULL, "Test network connectivity to MQTT broker", cmd_mqtt_test_connection),
        SHELL_SUBCMD_SET_END);
#endif

#ifdef CONFIG_RPR_MQTT_LOG_CLIENT
/**
 * @brief MQTT log client status command
 */
static int cmd_mqtt_log_status(const struct shell *sh, size_t argc, char **argv)
{
    bool initialized, fs_overflow_enabled;
    size_t buffer_count, buffer_capacity, fs_log_count;
    
    mqtt_log_client_get_status(&initialized, &fs_overflow_enabled, 
                               &buffer_count, &buffer_capacity,
                               &fs_log_count);
    
    shell_print(sh, "MQTT Log Client Status:");
    shell_print(sh, "  Initialized: %s", initialized ? "Yes" : "No");
    shell_print(sh, "  Buffer: %zu/%zu entries", buffer_count, buffer_capacity);
    shell_print(sh, "  Filesystem overflow: %s", fs_overflow_enabled ? "ENABLED" : "DISABLED");
    if (fs_overflow_enabled) {
        shell_print(sh, "  FS overflow path: /lfs/mqtt_logs.bin");
        shell_print(sh, "  Logs spilled to FS: %zu", fs_log_count);
    }
    
    return 0;
}

/**
 * @brief Test MQTT log filesystem overflow
 */
static int cmd_mqtt_log_test_fs(const struct shell *sh, size_t argc, char **argv)
{
    struct fs_file_t file;
    int ret;
    
    shell_print(sh, "Testing filesystem for MQTT log overflow...");
    
    /* Test file manager init */
#ifdef CONFIG_RPR_MODULE_FILE_MANAGER
    ret = file_manager_init();
    shell_print(sh, "File manager init: %d", ret);
#endif
    
    /* Test file creation */
    fs_file_t_init(&file);
    ret = fs_open(&file, "/lfs/test.txt", FS_O_CREATE | FS_O_RDWR);
    if (ret < 0) {
        shell_error(sh, "Failed to create test file: %d", ret);
        shell_print(sh, "Filesystem may not be mounted");
    } else {
        shell_print(sh, "Successfully created test file");
        fs_close(&file);
        
        /* Try to create the actual log file */
        ret = fs_open(&file, "/lfs/mqtt_logs.bin", FS_O_CREATE | FS_O_RDWR);
        if (ret < 0) {
            shell_error(sh, "Failed to create MQTT log file: %d", ret);
        } else {
            shell_print(sh, "Successfully created MQTT log file");
            fs_close(&file);
        }
    }
    
    return 0;
}

/**
 * @brief Delete MQTT log file
 */
static int cmd_mqtt_log_delete(const struct shell *sh, size_t argc, char **argv)
{
    int ret = fs_unlink("/lfs/mqtt_logs.bin");
    if (ret == 0) {
        shell_print(sh, "Successfully deleted /lfs/mqtt_logs.bin");
    } else {
        shell_error(sh, "Failed to delete /lfs/mqtt_logs.bin: %d", ret);
    }
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
        sub_mqtt_log,
        SHELL_CMD(status, NULL, "Show MQTT log client status", cmd_mqtt_log_status),
        SHELL_CMD(test_fs, NULL, "Test filesystem for overflow", cmd_mqtt_log_test_fs),
        SHELL_CMD(delete, NULL, "Delete MQTT log file", cmd_mqtt_log_delete),
        SHELL_SUBCMD_SET_END);
#endif

/**
 * @brief Shell command to display board info, firmware and hardware version information.
 */
static int cmd_device_info(const struct shell *sh, size_t argc, char **argv)
{
    LOG_INF("Device info command received");
    
    size_t      len    = 0;
    const char *fw_ver = dev_info_get_fw_version_str(&len);

    if (len > 0) {
        shell_print(sh, "Firmware version: V%s", fw_ver);
    } else {
        shell_error(sh, "Failed to retrieve firmware version.");
    }

    len                = 0;
    const char *hw_rev = dev_info_get_hw_revision_str(&len);

    if (len > 0) {
        shell_print(sh, "Hardware version: %s", hw_rev);
    } else {
        shell_error(sh, "Failed to retrieve hardware version.");
    }

    shell_print(sh, "Board name: %s", dev_info_get_board_name_str());

    len                = 0;
    const char *dev_id = dev_info_get_device_id_str(&len);

    if (len > 0) {
        shell_print(sh, "Device ID: %s", dev_id);
    } else {
        shell_error(sh, "Failed to retrieve device id.");
    }
    
    /* Add uptime */
    uint32_t uptime_ms = k_uptime_get_32();
    uint32_t uptime_sec = uptime_ms / 1000;
    uint32_t hours = uptime_sec / 3600;
    uint32_t minutes = (uptime_sec % 3600) / 60;
    uint32_t seconds = uptime_sec % 60;
    shell_print(sh, "Uptime: %02u:%02u:%02u", hours, minutes, seconds);
    
    /* Add IP address - check different interfaces */
    struct net_if *iface;
    const struct net_if_config *cfg;
    
#ifdef CONFIG_RPR_ETHERNET
    iface = net_if_get_default();
    if (iface) {
        cfg = net_if_get_config(iface);
        if (cfg && cfg->ip.ipv4) {
            char addr_str[NET_IPV4_ADDR_LEN];
            net_addr_ntop(AF_INET, &cfg->ip.ipv4->unicast[0].ipv4.address.in_addr, 
                         addr_str, sizeof(addr_str));
            shell_print(sh, "IP Address: %s", addr_str);
        }
    }
#endif

    return 0;
}

/**
 * @brief Simple test command that outputs "Hello".
 */
static int cmd_test(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "Hello");
    return 0;
}

/* ------------ Root app Command ------------ */
SHELL_STATIC_SUBCMD_SET_CREATE(
        sub_app,
        SHELL_CMD(led, &sub_led, "LED control", NULL),
        SHELL_CMD(watchdog, &sub_watchdog, "Watchdog control", NULL),
        SHELL_CMD(microphone, NULL, "Microphone control", cmd_microphone),
        SHELL_CMD(charger, &sub_charger, "Charger control", NULL),
        SHELL_CMD(battery, NULL, "Battery status", cmd_battery_get_status),
        SHELL_CMD(net, &sub_internet, "Internet control", NULL),
        SHELL_CMD(info, NULL, "Print device info", cmd_device_info),
        SHELL_CMD(audio, &audio_cmds, "Audio player commands", NULL),
        SHELL_CMD_ARG(switch,
                      NULL,
                      "Read switch state by index (0-3)",
                      cmd_switch_read,
                      2,
                      0),
        SHELL_CMD(poweroff, NULL, "System shutdown", cmd_poweroff),
        SHELL_CMD(dfu, &sub_dfu, "DFU management commands", NULL),
        SHELL_CMD(rtc, &rtc_cmds, "RTC commands", NULL),
        SHELL_CMD(test, NULL, "Test command", cmd_test),
        SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(app, &sub_app, "Application commands", NULL);
#ifdef CONFIG_RPR_MODULE_MQTT
SHELL_CMD_REGISTER(mqtt, &sub_mqtt, "MQTT client control", NULL);
#endif
#ifdef CONFIG_RPR_MQTT_LOG_CLIENT
SHELL_CMD_REGISTER(mqtt_log, &sub_mqtt_log, "MQTT log client control", NULL);
#endif