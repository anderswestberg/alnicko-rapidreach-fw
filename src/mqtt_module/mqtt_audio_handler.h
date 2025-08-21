/**
 * @file mqtt_audio_handler.h
 * @brief MQTT audio message handler for RapidReach
 * 
 * Handles MQTT messages containing audio alerts, parsing JSON metadata
 * and triggering Opus audio playback.
 */

#ifndef MQTT_AUDIO_HANDLER_H
#define MQTT_AUDIO_HANDLER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle incoming MQTT audio alert message
 * 
 * This function is called when an MQTT message is received on the audio alert topic.
 * It parses the message using the MQTT message parser and triggers audio playback.
 * 
 * @param topic MQTT topic the message was received on
 * @param payload Message payload containing JSON + Opus data
 * @param payload_len Length of the payload
 */
void mqtt_audio_alert_handler(const char *topic, const uint8_t *payload, size_t payload_len);

/**
 * @brief Register MQTT audio alert handler
 * 
 * Subscribes to the audio alert topic and registers the handler function.
 * 
 * @return 0 on success, negative error code on failure
 */
int mqtt_audio_handler_init(void);

/**
 * @brief Get audio alert topic for this device
 * 
 * @return Topic string (e.g., "rapidreach/audio/<device-id>")
 */
const char *mqtt_get_audio_alert_topic(void);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_AUDIO_HANDLER_H */
