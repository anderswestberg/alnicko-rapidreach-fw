/**
 * @file alnicko_shell.c
 * @brief Example implementation of shell commands for interacting with the Alnicko test server.
 *
 * This module provides shell commands to:
 * - retrieve and update RTC time from the server,
 * - list and download audio files by name or index,
 * - retrieve and download firmware updates,
 * - send custom messages to the server via HTTP POST.
 *
 * It serves as a demonstration of using shell-based user interaction for testing server APIs.
 * 
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/kernel.h>
#include <stdlib.h>

#include "rtc.h"
#include "alnicko_server.h"

/**
 * @brief Shell command to retrieve and print time from the server.
 *
 * Sends a request to the server to get the current time and displays it.
 * Prints an error message if the request fails.
 */
static int
cmd_alnicko_time_get(const struct shell *sh, size_t argc, char **argv)
{
    struct rtc_time t;
    server_status_t ret = alnicko_server_get_time(&t);

    if (ret == SERVER_OK) {
        shell_info(sh,
                   "Time from server: %04d-%02d-%02d %02d:%02d:%02d",
                   t.tm_year,
                   t.tm_mon,
                   t.tm_mday,
                   t.tm_hour,
                   t.tm_min,
                   t.tm_sec);
    } else {
        shell_error(sh, "Failed to get time. Error code: %d", ret);
    }

    return ret;
}

/**
 * @brief Shell command to update device RTC time from the server.
 *
 * Sends a request to the server to retrieve the current time and updates
 * the local RTC accordingly. Displays the result of the operation.
 */
static int
cmd_alnicko_time_update(const struct shell *sh, size_t argc, char **argv)
{

    shell_print(sh, "Updating time from server...");

    server_status_t ret = alnicko_server_update_time();
    if (ret == SERVER_OK) {
        shell_info(sh, "Time successfully updated from server.");
    } else {
        shell_error(sh, "Failed to update time. Error code: %d", ret);
    }

    return ret;
}

/**
 * @brief Shell command to list available audio files on the server.
 *
 * Retrieves and displays the list of audio file names currently stored on the server.
 * Shows an error message if the request fails.
 */
static int
cmd_alnicko_audio_list(const struct shell *sh, size_t argc, char **argv)
{
    char  name_buf[AUDIO_FILES_MAX][AUDIO_NAME_MAX_LEN];
    char *names[AUDIO_FILES_MAX];
    for (size_t i = 0; i < AUDIO_FILES_MAX; i++) {
        names[i] = name_buf[i];
    }

    size_t          count = 0;
    server_status_t ret =
            alnicko_server_get_audio_list(name_buf, AUDIO_FILES_MAX, &count);
    if (ret != SERVER_OK) {
        shell_error(sh, "Failed to retrieve audio list. Error code: %d", ret);
        return ret;
    }

    shell_info(sh, "Audio files on server (%d):", (int)count);
    for (size_t i = 0; i < count; i++) {
        shell_info(sh, "%d) %s", i + 1, names[i]);
    }

    return 0;
}

/**
 * @brief Shell command to download an audio file by name from the server.
 *
 * Downloads the specified audio file from the server using its filename.
 * Prints the result of the operation to the shell.
 */
static int
cmd_alnicko_audio_get_name(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2) {
        shell_error(sh, "Usage: alnicko audio get name <filename>");
        return -EINVAL;
    }

    const char     *filename = argv[1];
    server_status_t ret      = alnicko_server_get_audio_by_name(filename);

    if (ret == SERVER_OK) {
        shell_info(sh, "Audio '%s' downloaded successfully.", filename);
    } else {
        shell_error(sh,
                    "Failed to download audio '%s'. Error code: %d",
                    filename,
                    ret);
    }

    return ret;
}

/**
 * @brief Shell command to download an audio file by index from the server.
 *
 * Retrieves the audio filename from the server by its index in the list,
 * then downloads it. Displays the result in the shell.
 */
static int
cmd_alnicko_audio_get_num(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2) {
        shell_error(sh, "Usage: alnicko audio get num <index>");
        return -EINVAL;
    }

    uint8_t index = atoi(argv[1]);

    server_status_t ret = alnicko_server_get_audio_by_num(index);

    if (ret == SERVER_OK) {
        shell_info(sh, "Audio at index %d downloaded successfully.", index);
    } else {
        shell_error(sh,
                    "Failed to download audio at index %d. Error code: %d",
                    index,
                    ret);
    }

    return ret;
}

/**
 * @brief Shell command to retrieve the latest firmware name from the server.
 *
 * Sends a GET request to fetch the firmware update filename and prints it
 * to the shell if successful.
 */
static int
cmd_alnicko_update_name(const struct shell *sh, size_t argc, char **argv)
{
    char            name_buf[FW_UPDATE_NAME_MAX_LEN] = { 0 };
    server_status_t ret =
            alnicko_server_get_update_fw_name(name_buf, sizeof(name_buf));

    if (ret == SERVER_OK) {
        shell_info(sh, "Latest firmware name: %s", name_buf);
    } else {
        shell_error(sh, "Failed to get firmware name. Error code: %d", ret);
    }

    return ret;
}

/**
 * @brief Shell command to download the latest firmware update from the server.
 *
 * Retrieves the firmware name from the server and then downloads the corresponding update file.
 */
static int
cmd_alnicko_update_get(const struct shell *sh, size_t argc, char **argv)
{
    server_status_t ret = alnicko_server_get_update_fw();

    if (ret == SERVER_OK) {
        shell_info(sh, "Firmware update downloaded successfully.");
    } else {
        shell_error(
                sh, "Failed to download firmware update. Error code: %d", ret);
    }

    return ret;
}

/**
 * @brief Shell command to send a message to the server via HTTP POST.
 *
 * Sends a user-defined message as a JSON payload to the server.
 */
static int
cmd_alnicko_post_message(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: alnicko post <message>");
        return -EINVAL;
    }

    const char *payload = argv[1];

    server_status_t ret = alnicko_server_post_message(payload);

    if (ret == SERVER_OK) {
        shell_info(sh, "Message sent successfully.");
    } else {
        shell_error(sh, "Failed to send message. Error code: %d", ret);
    }

    return ret;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
        alnicko_time_subcmds,
        SHELL_CMD(get, NULL, "Get time from server", cmd_alnicko_time_get),
        SHELL_CMD(update,
                  NULL,
                  "Update RTC from server",
                  cmd_alnicko_time_update),
        SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(alnicko_audio_get_cmds,
                               SHELL_CMD_ARG(name,
                                             NULL,
                                             "Download audio by filename",
                                             cmd_alnicko_audio_get_name,
                                             2,
                                             2),
                               SHELL_CMD_ARG(num,
                                             NULL,
                                             "Download audio by index",
                                             cmd_alnicko_audio_get_num,
                                             2,
                                             2),
                               SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
        alnicko_audio_cmds,
        SHELL_CMD(list,
                  NULL,
                  "List available audio files from server",
                  cmd_alnicko_audio_list),
        SHELL_CMD(get,
                  &alnicko_audio_get_cmds,
                  "Download audio by name or index",
                  NULL),
        SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(alnicko_update_cmds,
                               SHELL_CMD(name,
                                         NULL,
                                         "Get firmware update name",
                                         cmd_alnicko_update_name),
                               SHELL_CMD(get,
                                         NULL,
                                         "Download firmware update",
                                         cmd_alnicko_update_get),
                               SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
        alnicko_subcmds,
        SHELL_CMD(time, &alnicko_time_subcmds, "Time operations", NULL),
        SHELL_CMD(audio, &alnicko_audio_cmds, "Audio-related operations", NULL),
        SHELL_CMD(update,
                  &alnicko_update_cmds,
                  "Firmware update operations",
                  NULL),
        SHELL_CMD_ARG(post,
                      NULL,
                      "Post message to server: alnicko post <message>",
                      cmd_alnicko_post_message,
                      2,
                      SHELL_OPT_ARG_MAX),
        SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(alnicko, &alnicko_subcmds, "Alnicko server command", NULL);
