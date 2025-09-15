/**
 * @file mqtt_topic_match.c
 * @brief MQTT topic matching with wildcard support
 */

#include <string.h>
#include <stdbool.h>

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
bool mqtt_topic_match(const char *pattern, const char *topic)
{
    const char *p = pattern;
    const char *t = topic;
    
    while (*p && *t) {
        if (*p == '+') {
            /* Single level wildcard - skip to next '/' in topic */
            while (*t && *t != '/') {
                t++;
            }
            p++;
            if (*p == '/') {
                p++;
                if (*t == '/') {
                    t++;
                } else {
                    return false;
                }
            }
        } else if (*p == '#') {
            /* Multi-level wildcard - must be at end of pattern */
            return (*(p + 1) == '\0');
        } else if (*p == *t) {
            /* Exact character match */
            p++;
            t++;
        } else {
            /* Mismatch */
            return false;
        }
    }
    
    /* Check if we've consumed both strings entirely */
    return (*p == '\0' && *t == '\0') || (*p == '#' && *(p + 1) == '\0');
}

