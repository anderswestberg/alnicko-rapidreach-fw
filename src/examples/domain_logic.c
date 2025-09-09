/**
 * @file domain_logic.c
 * @brief Demonstration of full-featured application logic using firmware modules.
 *
 * This example shows how to integrate and coordinate various firmware components,
 * including the power supervisor, audio codec and Opus decoder, test server (Alnicko),
 * LED/button/switch handling, and networking (Ethernet, Wi-Fi, LTE, HTTP).
 * Intended to be called from main(), it serves as a practical template for application logic.
 * 
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#include <zephyr/kernel.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "domain_logic.h"
#include "power_supervisor.h"
#include "led_control.h"
#include "poweroff.h"
#include "dev_info.h"
#include "switch_module.h"
#include "microphone.h"

#include "audio_player.h"

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/fs/fs.h>

#ifdef CONFIG_RPR_MODEM
#include "modem_module.h"
#endif

#ifdef CONFIG_RPR_ETHERNET
#include "ethernet.h"
#endif

#ifdef CONFIG_RPR_WIFI
#include "wifi.h"
#define WIFI_CONFIG_JSON_MAX_LEN 256
#define FULL_FILE_PATH_MAX_LEN \
    (CONFIG_RPR_FOLDER_PATH_MAX_LEN + CONFIG_RPR_FILENAME_MAX_LEN)
#endif

#ifdef CONFIG_RPR_MODULE_HTTP
#include "alnicko_server.h"
#endif

#ifdef CONFIG_HTTP_LOG_CLIENT
#include "http_log_client.h"
#endif

#ifdef CONFIG_RPR_MODULE_MQTT
#include "../mqtt_module/mqtt_module.h"
#endif

#ifdef CONFIG_RPR_MODULE_DEVICE_REGISTRY
#include "../device_registry/device_registry.h"
#endif

#ifdef CONFIG_RPR_MODULE_INIT_SM
#include "../init_state_machine/init_state_machine.h"
#endif

#ifdef CONFIG_RPR_MODULE_DFU
#include "dfu_manager.h"
#endif

#include "rtc.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(domain_logic, CONFIG_EXAMPLES_DOMAIN_LOGIC_LOG_LEVEL);

#define EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)

#define NET_LINK_LED               2
#define LED_BLINK_PROCESS_ON       50
#define LED_BLINK_PROCESS_OFF      500
#define MSG_BUF_SIZE               256
#define DEFAULT_SHUTDOWN_DELAY_MS  1000
#define NET_APP_DELAY_MS           2000
#define DOMAIN_LOGIC_LOOP_DELAY_MS 500

#define TARGET_AUDIO_FILE CONFIG_EXAMPLES_DOMAIN_LOGIC_TARGET_AUDIO_FILE

#define NET_CONNECT_TIMEOUT_MS \
    K_MSEC(CONFIG_EXAMPLES_DOMAIN_LOGIC_NET_CONNECT_TIMEOUT_MS)

typedef enum {
    NET_CONNECT_INIT = 0,
    NET_CONNECT_ETHERNET,
    NET_CONNECT_WIFI,
    NET_CONNECT_LTE,
    NET_CONNECT_NO_INTERFACE
} network_status_t;

struct network_context {
    network_status_t               status;
    struct net_if                 *iface;
    struct net_mgmt_event_callback mgmt_cb;
    bool                           connected;
    struct k_sem                   net_app_sem;
    struct k_sem                   audio_play_sem;
    struct k_sem                   ping_thread_sem;
    uint16_t                       domain_ping_count;
    uint16_t                       audio_ping_count;
};

static struct network_context net_ctx = {
    .status            = NET_CONNECT_INIT,
    .iface             = NULL,
    .connected         = false,
    .domain_ping_count = 0,
    .audio_ping_count  = 0,
};

#ifdef CONFIG_HTTP_LOG_CLIENT
/* Forward declaration */
static void init_http_log_client(void);
#endif

/**
 * @brief Callback for short press of the power-off button.
 *
 * This function is triggered when the power-off button is briefly pressed.
 * It releases the semaphore to notify the audio playback thread to proceed.
 */
static void power_off_short_pressed(void)
{
    LOG_DBG("User callback for short pressed power off button");

    k_sem_give(&net_ctx.audio_play_sem);
}

/**
 * @brief Handles audio playback based on switch combination.
 *
 * This function is triggered when the audio playback semaphore is given.
 * If no audio is currently playing, it reads a 4-switch combination to determine
 * which audio file to play.
 * If the index is valid and the file is found, playback is started.
 * If audio is already playing, the function stops playback.
 *
 * Error handling includes invalid switch states, missing audio files, or playback issues.
 */
static void audio_play(void)
{

    if (k_sem_take(&net_ctx.audio_play_sem, K_NO_WAIT) != 0) {
        return;
    }

    if (!get_playing_status()) {

        // Read 4 switch states (binary combination => target_index)
        int target_index = 0;
        for (int i = 0; i < 4; i++) {
            int state = switch_get_state(i);
            if (state < 0) {
                LOG_ERR("Failed to read switch %d (err %d)", i, state);
                return;
            }
            target_index |= (state ? 1 : 0) << i;
        }

        // Audio files indexed from 1
        if (target_index == 0) {
            LOG_WRN("Switch combination resulted in index 0, no file selected");
            return;
        }

        struct fs_dir_t  dir;
        struct fs_dirent entry;
        fs_dir_t_init(&dir);

        if (fs_opendir(&dir, CONFIG_RPR_AUDIO_DEFAULT_PATH) != 0) {
            LOG_ERR("Failed to open directory");
            return;
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
            LOG_ERR("File with index %d not found", target_index);
            return;
        }

        player_status_t status = -1;

        while (!get_playing_status()) {

            status = audio_player_start(full_path);

            if (status != PLAYER_OK)
                break;

            k_msleep(100);
        }

        switch (status) {
        case PLAYER_OK:
            LOG_INF("Audio playback started successfully");
            break;
        case PLAYER_ERROR_CODEC_INIT:
            LOG_ERR("Error: Audio device is not initialized");
            break;
        case PLAYER_ERROR_BUSY:
            LOG_ERR("Error: Audio device is already playing");
            break;
        case PLAYER_EMPTY_DATA:
            LOG_ERR("Error: Provided Opus data is empty or invalid");
            break;
        default:
            LOG_ERR("Error: Unknown playback error (code %d)", status);
            break;
        }
    } else {

        player_status_t status = audio_player_stop();

        switch (status) {
        case PLAYER_OK:
            LOG_INF("Audio playback stopped");
            break;
        case PLAYER_ERROR_CODEC_INIT:
            LOG_ERR("Error: Audio device is not initialized");
            break;
        default:
            LOG_ERR("Error: Unknown stop error (code %d)", status);
            break;
        }
    }
}

/**
 * @brief Handles network management events (L4 connection/disconnection).
 *
 * This callback is triggered by Zephyr's network management layer upon
 * relevant L4 network events (connected or disconnected).
 *
 * @param cb          Pointer to the event callback structure.
 * @param mgmt_event  Network event type (connect/disconnect).
 * @param iface       Network interface.
 */
static void net_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                   uint32_t                        mgmt_event,
                                   struct net_if                  *iface)
{
    if ((mgmt_event & EVENT_MASK) != mgmt_event) {
        return;
    }
    if (mgmt_event == NET_EVENT_L4_CONNECTED) {

#if defined(CONFIG_RPR_ETHERNET)
        if (is_ethernet_iface(iface)) {
            if (ethernet_set_iface_default() == ETHERNET_OK) {
                net_ctx.status = NET_CONNECT_ETHERNET;
            } else {
                LOG_ERR("Failed to set Ethernet as default interface");
            }
        } else
#endif
#if defined(CONFIG_RPR_WIFI)
                if (is_wifi_iface(iface)) {
            if (wifi_set_iface_default() == WIFI_OK) {
                net_ctx.status = NET_CONNECT_WIFI;

            } else {
                LOG_ERR("Failed to set Wi-Fi as default interface");
            }
        } else
#endif
#if defined(CONFIG_RPR_MODEM)
                if (is_modem_iface(iface)) {
            if (modem_set_iface_default() == MODEM_SUCCESS) {
                net_ctx.status = NET_CONNECT_LTE;
            } else {
                LOG_ERR("Failed to set Modem as default interface");
            }
        } else
#endif
        {
            net_ctx.status = NET_CONNECT_NO_INTERFACE;
            LOG_WRN("Connected interface type is not recognized");
        }

        LOG_INF("Network connected");
        net_ctx.connected = true;
        led_on(NET_LINK_LED);
        
#ifdef CONFIG_HTTP_LOG_CLIENT
        /* Initialize HTTP log client now that we have network */
        init_http_log_client();
#endif

#ifdef CONFIG_RPR_MODULE_MQTT
        /* Check if RTC is ready and has valid time before initializing MQTT */
        struct rtc_time current_time;
        int rtc_ret = get_date_time(&current_time);
        bool rtc_valid = false;
        
        if (rtc_ret == 0) {
            /* The RTC might be storing the actual year (like 2025) in tm_year
             * instead of years since 1900. Check for both cases. */
            int actual_year;
            if (current_time.tm_year > 1900) {
                /* RTC is storing actual year */
                actual_year = current_time.tm_year;
            } else {
                /* RTC is storing years since 1900 (standard) */
                actual_year = current_time.tm_year + 1900;
            }
            
            /* Check if year is reasonable (2020-2099) */
            if (actual_year >= 2020 && actual_year <= 2099) {
                LOG_INF("RTC has valid time: %04d-%02d-%02d %02d:%02d:%02d",
                        actual_year, current_time.tm_mon + 1,
                        current_time.tm_mday, current_time.tm_hour,
                        current_time.tm_min, current_time.tm_sec);
                rtc_valid = true;
            } else {
                LOG_WRN("RTC time invalid - year %d (expected 2020-2099)", actual_year);
            }
        } else {
            LOG_WRN("RTC not ready yet (ret=%d)", rtc_ret);
        }
        
        if (rtc_valid) {
            /* Initialize MQTT module now that we have network and valid RTC */
            int ret = mqtt_init();
            if (ret == 0) {
                LOG_INF("MQTT module initialized");
                /* Connect to MQTT broker */
                mqtt_status_t status = mqtt_module_connect();
                if (status == MQTT_SUCCESS) {
                    LOG_INF("MQTT connection initiated");
                    /* Start heartbeat */
                    mqtt_start_heartbeat();
                } else {
                    LOG_ERR("Failed to connect to MQTT broker: %d", status);
                }
            } else {
                LOG_ERR("Failed to initialize MQTT module: %d", ret);
            }
        } else {
            LOG_WRN("Delaying MQTT initialization until RTC has valid time");
            /* TODO: Schedule a delayed work item to retry MQTT init when RTC is ready */
        }
#endif
        
        k_sem_give(&net_ctx.net_app_sem);
        return;
    }
    if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
        if (net_ctx.connected == false) {
            LOG_INF("Waiting for network to be connected");
        } else {
            led_off(NET_LINK_LED);

            LOG_INF("Network disconnected");
            net_ctx.connected = false;
            net_ctx.status    = NET_CONNECT_NO_INTERFACE;
        }
        k_sem_reset(&net_ctx.net_app_sem);
        return;
    }
}

/**
 * @brief Pings main and audio threads to ensure system responsiveness.
 *
 * This function is used as a callback by the power supervisor to verify
 * that the main and audio threads are still responsive.
 *
 * @return true if both threads responded correctly, false otherwise.
 */
static bool domain_logic_ping(void)
{

    if (k_sem_take(&net_ctx.ping_thread_sem, K_NO_WAIT) == 0) {
        net_ctx.domain_ping_count = 0;
    } else {
        if (net_ctx.domain_ping_count >
            CONFIG_EXAMPLES_DOMAIN_LOGIC_MAX_PING_LOSSES) {
            LOG_ERR("Ping timeout: no reply from main thread.");
            return false;
        } else {
            net_ctx.domain_ping_count++;
        }
    }

    if (get_pause_status()) {
        LOG_WRN("Ping is not available while playback is paused.");
    } else {
        uint32_t result = audio_player_ping();

        if (!(result & AUDIO_EVT_PING_REPLY) &&
            !(result & AUDIO_EVT_PING_STOP)) {

            if (net_ctx.audio_ping_count >
                CONFIG_EXAMPLES_DOMAIN_LOGIC_AUDIO_MAX_PING_LOSSES) {
                LOG_ERR("Ping timeout: no reply from audio thread.");
                return false;
            } else {
                net_ctx.audio_ping_count++;
            }
        }
        net_ctx.audio_ping_count = 0;
    }

    return true;
}

/**
 * @brief Gracefully deinitializes application logic during shutdown.
 *
 * This function is intended as a callback for the power supervisor to perform
 * final cleanup before the device powers off.
 */
static void domain_logic_deinit(void)
{
    audio_player_stop();

    struct rtc_time t;
    int             ret = get_date_time(&t);
    if (ret < 0) {
        LOG_ERR("Failed to get RTC time: %d", ret);
        return;
    }

    LOG_INF("Shutdown occurred on %04d/%02d/%02d at %02d:%02d.",
            t.tm_year,
            t.tm_mon,
            t.tm_mday,
            t.tm_hour,
            t.tm_min);

    if (net_ctx.connected) {
#ifdef CONFIG_RPR_MODULE_HTTP

        const char *dev_name = dev_info_get_board_name_str();
        if (!dev_name) {
            LOG_ERR("Failed to retrieve board name.");
            return;
        }

        int         len    = 0;
        const char *dev_id = dev_info_get_device_id_str(&len);
        if (len == 0 || dev_id == NULL) {
            LOG_ERR("Failed to retrieve device ID.");
            return;
        }

        char msg[MSG_BUF_SIZE];
        snprintf(msg,
                 sizeof(msg),
                 "Device %s (%s) is powered down",
                 dev_id,
                 dev_name);

        alnicko_server_post_message(msg);
    }
#endif

    switch (net_ctx.status) {

    case NET_CONNECT_WIFI:
#ifdef CONFIG_RPR_WIFI
        wifi_start_disconnect();
        struct net_if *iface = net_if_get_default();
        if (iface) {
            net_if_down(iface);
            LOG_INF("Network interface down.");
            k_msleep(DEFAULT_SHUTDOWN_DELAY_MS);
        }
        break;
#endif

    case NET_CONNECT_LTE:
#ifdef CONFIG_RPR_MODEM
        modem_shutdown();
#endif
        break;

    default:
        k_msleep(DEFAULT_SHUTDOWN_DELAY_MS);
        break;
    }
}

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
 * @brief Wait for the L4 (Layer 4) network connection to be established.
 *
 * Blocks until the default network interface reports a NET_EVENT_L4_CONNECTED event
 * or the timeout expires.
 *
 * @return 0 if the event occurred successfully, negative error code on timeout or failure.
 */
static int wait_for_l4_connection(void)
{
    struct net_if *iface = net_if_get_default();
    LOG_DBG("Waiting for L4 connection...");

    return net_mgmt_event_wait_on_iface(iface,
                                        NET_EVENT_L4_CONNECTED,
                                        NULL,
                                        NULL,
                                        NULL,
                                        NET_CONNECT_TIMEOUT_MS);
}

/**
 * @brief Attempt to connect to the network using available interfaces.
 *
 * Tries Ethernet, Wi-Fi, and LTE modem (in that order, if enabled).
 * On success, sets the corresponding connection status in net_ctx.
 * On failure, marks the connection as failed and disables the network LED.
 */
void network_attempt_connect(void)
{
    net_ctx.status = NET_CONNECT_INIT;

    if (led_blink(NET_LINK_LED, LED_BLINK_PROCESS_ON, LED_BLINK_PROCESS_OFF) !=
        LED_OK) {
        LOG_WRN("Failed to blink network LED");
    }

#ifdef CONFIG_RPR_ETHERNET
    LOG_INF("Trying Ethernet...");
    if (wait_for_l4_connection() == 0) {
        LOG_INF("Connected via Ethernet");
        return;
    } else {
        LOG_WRN("Ethernet connection timeout");
    }

#endif

#ifdef CONFIG_RPR_WIFI
    LOG_INF("Trying Wi-Fi...");
    if (wifi_start_autoconnect(saved_network_lookup_cb) == WIFI_OK) {
        if (wait_for_l4_connection() == 0) {
            LOG_INF("Connected via Wi-Fi");
            return;
        } else {
            LOG_WRN("Wi-Fi connection timeout");
        }
    } else {
        LOG_ERR("Wi-Fi autoconnect failed");
    }

#endif

#ifdef CONFIG_RPR_MODEM
    LOG_INF("Trying LTE modem...");

    if ((modem_init_and_connect() == MODEM_SUCCESS) &&
        (wait_for_l4_connection() == 0)) {
        LOG_INF("Connected via LTE modem");
        return;

    } else {
        LOG_WRN("LTE connection timeout");
        modem_shutdown();
    }

#endif

    net_ctx.status = NET_CONNECT_NO_INTERFACE;
    led_off(NET_LINK_LED);

#ifdef CONFIG_RPR_ETHERNET
    if (ethernet_set_iface_default() != ETHERNET_OK) {
        LOG_WRN("Ethernet is NOT default network interface");
    }
#endif
    LOG_WRN("Failed to connect to any network interface");
}

#ifdef CONFIG_HTTP_LOG_CLIENT
/**
 * @brief Initialize HTTP log client after network connection
 */
static void init_http_log_client(void)
{
    size_t device_id_len;
    const char *device_id = dev_info_get_device_id_str(&device_id_len);
    
    struct http_log_config log_config = {
        .server_url = CONFIG_HTTP_LOG_CLIENT_DEFAULT_SERVER_URL,
        .device_id = device_id,
        .batch_size = 10,  /* Reduced to prevent stack overflow */
        .flush_interval_ms = 5000,
        .buffer_size = 100,  /* Reduced from 500 to fit in available RAM */
        .enable_compression = false
    };
    
    http_log_status_t ret = http_log_init(&log_config);
    if (ret == HTTP_LOG_SUCCESS) {
        LOG_INF("HTTP log client initialized for device: %s", device_id);
        /* Use direct function call instead of macro to avoid LOG_MODULE_NAME issue */
        ret = http_log_add(LOG_LEVEL_INF, "domain_logic", "Device connected and logging enabled");
        if (ret != HTTP_LOG_SUCCESS) {
            LOG_ERR("Failed to add test log: %d", ret);
        } else {
            LOG_INF("Test log added successfully");
        }
    } else {
        LOG_ERR("Failed to initialize HTTP log client: %d", ret);
    }
}
#endif

#ifdef CONFIG_RPR_MODULE_HTTP
/**
 * @brief Demonstration example of network application logic.
 *
 * This function showcases how to use HTTP-based server communication in Zephyr.
 * It demonstrates time synchronization with a remote server, device information reporting,
 * and conditional downloading of an audio file if it is not present in the filesystem.
 */
void net_application_start(void)
{
    if (k_sem_take(&net_ctx.net_app_sem, K_NO_WAIT) != 0) {
        return;
    }

    LOG_INF("Updating time from server...");

    server_status_t ret = alnicko_server_update_time();
    if (ret != SERVER_OK) {
        LOG_ERR("Failed to update time. Error code: %d", ret);
        return;
    }

    LOG_INF("Time successfully updated from server.");

    size_t      len    = 0;
    const char *fw_ver = dev_info_get_fw_version_str(&len);
    if (len == 0 || fw_ver == NULL) {
        LOG_ERR("Failed to retrieve firmware version.");
        return;
    }

    len                = 0;
    const char *hw_rev = dev_info_get_hw_revision_str(&len);
    if (len == 0 || hw_rev == NULL) {
        LOG_ERR("Failed to retrieve hardware version.");
        return;
    }

    const char *dev_name = dev_info_get_board_name_str();
    if (!dev_name) {
        LOG_ERR("Failed to retrieve board name.");
        return;
    }

    len                = 0;
    const char *dev_id = dev_info_get_device_id_str(&len);
    if (len == 0 || dev_id == NULL) {
        LOG_ERR("Failed to retrieve device ID.");
        return;
    }

    k_msleep(NET_APP_DELAY_MS);

    char msg[MSG_BUF_SIZE];
    snprintf(
            msg,
            sizeof(msg),
            "Device %s (%s) connected to network. HW version: %s, FW version: %s",
            dev_id,
            dev_name,
            hw_rev,
            fw_ver);

    alnicko_server_post_message(msg);

#ifdef CONFIG_EXAMPLES_DOMAIN_LOGIC_AUTO_DOWNLOAD_AUDIO
    if (!TARGET_AUDIO_FILE || strlen(TARGET_AUDIO_FILE) == 0) {
        LOG_ERR("No target audio file defined.");
        return;
    }

    if (get_playing_status()) {
        LOG_WRN("Cannot download audio file while playback is active.");
        return;
    }

    char path[FULL_AUDIO_PATH_MAX_LEN];
    snprintf(path,
             sizeof(path),
             "%s/%s",
             CONFIG_RPR_AUDIO_DEFAULT_PATH,
             TARGET_AUDIO_FILE);

    struct fs_dirent file_info;
    int              fs_res = fs_stat(path, &file_info);

    if (fs_res == 0 && file_info.type == FS_DIR_ENTRY_FILE) {
        LOG_INF("Audio file '%s' found at %s", TARGET_AUDIO_FILE, path);
    } else {
        k_msleep(NET_APP_DELAY_MS);

        LOG_INF("Audio file not found, downloading...");

        ret = alnicko_server_get_audio_by_name(TARGET_AUDIO_FILE);
        if (ret != SERVER_OK) {
            LOG_ERR("Failed to download audio '%s'. Error code: %d",
                    TARGET_AUDIO_FILE,
                    ret);
        } else {
            LOG_INF("Audio file '%s' successfully downloaded.",
                    TARGET_AUDIO_FILE);
        }
    }
#endif
}
#endif

#ifdef CONFIG_RPR_MODULE_DFU
/**
 * @brief Check and confirm the current DFU firmware image if not yet confirmed.
 *
 * This demonstration function verifies whether the currently running firmware
 * image is confirmed in the MCUboot DFU workflow. If not confirmed, it attempts
 * to mark the image as confirmed. Additionally, it logs the current firmware
 * version and the firmware version stored in the DFU (secondary) slot.
 */
static void check_firmware(void)
{
    int ret = dfu_is_current_img_confirmed();

    if (ret < 0) {
        LOG_ERR("Error checking image confirmation status (code: %d)", ret);
        return;
    } else if (ret == 0) {
        LOG_DBG("Current image is NOT confirmed. Attempting to confirm...");

        ret = dfu_mark_current_img_as_confirmed();
        if (ret == 0) {
            LOG_INF("Current image successfully confirmed.");
        } else {
            LOG_ERR("Failed to confirm current image (code: %d)", ret);
            return;
        }
    } else {
        LOG_DBG("Current image is already confirmed.");
    }

    // Get current firmware version
    const char *fw_ver     = NULL;
    size_t      fw_ver_len = 0;

    fw_ver = dev_info_get_fw_version_str(&fw_ver_len);
    if (!fw_ver || fw_ver_len == 0) {
        LOG_ERR("Failed to get current firmware version.");
        return;
    }

    LOG_INF("Current firmware version: %s", fw_ver);

    // Get version from DFU (secondary) slot
    char dfu_version[UPD_VERSION_STRING_MAX_LEN] = { 0 };
    if (dfu_get_fw_update_version_str(dfu_version, sizeof(dfu_version))) {
        LOG_INF("DFU slot firmware version: %s", dfu_version);
    } else {
        LOG_WRN("DFU slot firmware version not available.");
    }
}
#endif

/**
 * @brief Demonstration entry point for domain logic showcasing
 *        interaction with firmware modules.
 *
 * This function serves as a minimal example application to demonstrate how
 * various firmware components—such as the audio codec, Opus decoder,
 * networking stack, and power supervisor—can be integrated and executed together.
 *
 * It is intended to be called directly from the main function. The logic demonstrates:
 * - Firmware confirmation and version reporting
 * - Network auto-connect and event handling
 * - Periodic watchdog pinging
 * - Audio file download and playback
 *
 * This function is intended for developers as a example for integrating
 * their own domain logic using the provided firmware infrastructure.
 */
void domain_logic_func(void)
{
    LOG_INF("Domain logic starting...");
    
#ifdef CONFIG_RPR_MODULE_DFU
    check_firmware(); // Confirm or log current firmware state
#endif
    uint8_t mic_inactive_counter = 0;

    // Initialize internal semaphores
    k_sem_init(&net_ctx.net_app_sem, 0, 1);
    k_sem_init(&net_ctx.audio_play_sem, 0, 1);
    k_sem_init(&net_ctx.ping_thread_sem, 0, 1);

    // Register supervisor callbacks early to prevent watchdog timeout
    supervisor_ping_register_callback(domain_logic_ping);
    supervisor_poweroff_register_callback(domain_logic_deinit);
    
    // Register power button short press callbacks
    poweroff_register_short_push_callback(power_off_short_pressed);

#ifdef CONFIG_RPR_MODULE_INIT_SM
    // Initialize and start the initialization state machine
    init_state_machine_init();
    init_state_machine_start();
    
    // Wait for system to become operational
    LOG_INF("Waiting for system initialization...");
    uint32_t wait_count = 0;
    while (!init_state_machine_is_operational()) {
        k_msleep(100);
        wait_count++;
        
        if (wait_count % 10 == 0) {
            init_state_t current_state = init_state_machine_get_state();
            LOG_INF("Still waiting... current state: %d", current_state);
        }
        
        // Check if we're in error state with max retries exceeded
        if (init_state_machine_get_state() == STATE_ERROR) {
            LOG_ERR("System initialization failed");
            break;
        }
        
        // Timeout after 30 seconds
        if (wait_count > 300) {
            LOG_ERR("System initialization timeout");
            break;
        }
    }
    
    if (init_state_machine_is_operational()) {
        LOG_INF("System initialization complete, device ID: %s", 
                init_state_machine_get_device_id());
    }
#else
    // Legacy initialization sequence
#ifdef CONFIG_RPR_MODULE_DEVICE_REGISTRY
    // Initialize device registry for deferred registration
    device_registry_init();
#endif

    // Setup network event handling
    net_mgmt_init_event_callback(
            &net_ctx.mgmt_cb, net_mgmt_event_handler, EVENT_MASK);
    net_mgmt_add_event_callback(&net_ctx.mgmt_cb);

#ifdef CONFIG_EXAMPLES_DOMAIN_LOGIC_AUTO_CONNECT_ON_START
    network_attempt_connect();
#endif
#endif /* CONFIG_RPR_MODULE_INIT_SM */

    // Main loop: ping watchdog, run app logic, trigger audio
    while (true) {
        k_msleep(DOMAIN_LOGIC_LOOP_DELAY_MS);
        k_sem_give(&net_ctx.ping_thread_sem);
        net_application_start();
        audio_play();

        // While audio is playing, check for microphone input
        if (get_playing_status()) {
            if (microphone_is_sound_detected()) {
                LOG_DBG("Microphone activity detected during playback.");
                mic_inactive_counter = 0;
            } else {
                mic_inactive_counter++;
            }
            if (mic_inactive_counter >=
                CONFIG_EXAMPLES_DOMAIN_LOGIC_MIC_INACTIVE_THRESHOLD) {
                LOG_WRN("No microphone input detected during playback.");
            }
        }
    }
}
