/**
 * @file mqtt_topic_match.h
 * @brief MQTT topic matching with wildcard support
 */

#ifndef MQTT_TOPIC_MATCH_H
#define MQTT_TOPIC_MATCH_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if a topic matches a subscription pattern with wildcards
 * 
 * Supports MQTT wildcards:
 * - '+' matches exactly one topic level
 * - '#' matches any number of topic levels (must be at end)
 * 
 * @param pattern Subscription pattern (may contain wildcards)
 * @param topic Actual topic to match
 * @return true if topic matches pattern, false otherwise
 */
bool mqtt_topic_match(const char *pattern, const char *topic);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_TOPIC_MATCH_H */

