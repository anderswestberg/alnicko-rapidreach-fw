/**
 * @file init_state_machine.h
 * @brief Device initialization state machine for managing network, device registration, and MQTT setup
 *
 * This state machine ensures proper sequencing of:
 * 1. Network connectivity
 * 2. Device registration (if enabled)
 * 3. MQTT initialization
 *
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#ifndef __INIT_STATE_MACHINE_H__
#define __INIT_STATE_MACHINE_H__

#include <zephyr/kernel.h>
#include <stdbool.h>

/**
 * @brief Initialization states
 */
typedef enum {
    STATE_INIT = 0,                  /**< Initial state after boot */
    STATE_WAIT_NETWORK,              /**< Waiting for network connectivity */
    STATE_NETWORK_READY,             /**< Network available, ready to proceed */
    STATE_NETWORK_STABILIZE,         /**< Delay state to let network stabilize */
    STATE_DEVICE_REG_START,          /**< Starting device registration */
    STATE_DEVICE_REG_IN_PROGRESS,    /**< Registration in progress */
    STATE_DEVICE_REG_COMPLETE,       /**< Registration complete (success or fallback) */
    STATE_MQTT_INIT_START,           /**< Starting MQTT initialization */
    STATE_MQTT_CONNECTING,           /**< MQTT connection in progress */
    STATE_OPERATIONAL,               /**< Fully operational */
    STATE_ERROR,                     /**< Error state with retry capability */
    STATE_MAX
} init_state_t;

/**
 * @brief State machine events
 */
typedef enum {
    EVENT_START = 0,                 /**< Start the state machine */
    EVENT_NETWORK_UP,                /**< Network interface is up */
    EVENT_NETWORK_DOWN,              /**< Network interface is down */
    EVENT_REG_SUCCESS,               /**< Device registration succeeded */
    EVENT_REG_FAILURE,               /**< Device registration failed (use fallback) */
    EVENT_REG_RETRY_NEEDED,          /**< Registration needs retry */
    EVENT_MQTT_CONNECTED,            /**< MQTT connected successfully */
    EVENT_MQTT_DISCONNECTED,         /**< MQTT disconnected */
    EVENT_MQTT_FAILURE,              /**< MQTT connection failed */
    EVENT_TIMEOUT,                   /**< Operation timeout */
    EVENT_RETRY,                     /**< Retry current operation */
    EVENT_MAX
} init_event_t;

/**
 * @brief State machine context
 */
typedef struct {
    init_state_t current_state;
    init_state_t previous_state;
    bool network_available;
    bool device_registered;
    bool mqtt_connected;
    char device_id[32];              /**< Assigned device ID */
    uint8_t retry_count;
    uint32_t error_code;
    struct k_work_delayable timeout_work;
    struct k_work_delayable retry_work;
} init_sm_context_t;

/**
 * @brief State transition handler function type
 */
typedef void (*state_handler_t)(init_sm_context_t *ctx, init_event_t event);

/**
 * @brief State information structure
 */
typedef struct {
    const char *name;                /**< State name for logging */
    state_handler_t entry;           /**< Called on state entry */
    state_handler_t exit;            /**< Called on state exit */
    state_handler_t handler;         /**< Main state handler */
} state_info_t;

/**
 * @brief Initialize the state machine
 * @return 0 on success, negative error code on failure
 */
int init_state_machine_init(void);

/**
 * @brief Start the state machine
 */
void init_state_machine_start(void);

/**
 * @brief Send an event to the state machine
 * @param event Event to process
 */
void init_state_machine_send_event(init_event_t event);

/**
 * @brief Get current state
 * @return Current state
 */
init_state_t init_state_machine_get_state(void);

/**
 * @brief Check if system is operational
 * @return true if in operational state
 */
bool init_state_machine_is_operational(void);

/**
 * @brief Get the assigned device ID
 * @return Pointer to device ID string
 */
const char *init_state_machine_get_device_id(void);

#endif /* __INIT_STATE_MACHINE_H__ */
