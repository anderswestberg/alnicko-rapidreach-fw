/**
 * @file init_state_machine.c
 * @brief Implementation of device initialization state machine
 *
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <string.h>

#include "init_state_machine.h"

#ifdef CONFIG_RPR_MODULE_DEVICE_REGISTRY
#include "../device_registry/device_registry.h"
#endif

#ifdef CONFIG_RPR_MODULE_MQTT
#include "../mqtt_module/mqtt_module.h"
#include "../mqtt_module/mqtt_audio_handler.h"
#endif

#ifdef CONFIG_RPR_MQTT_LOG_CLIENT
#include "../mqtt_log_client/mqtt_log_client.h"
#endif

#include "../dev_info/dev_info.h"

LOG_MODULE_REGISTER(init_sm, CONFIG_RPR_MODULE_INIT_SM_LOG_LEVEL);

/* Timeout values */
#define NETWORK_WAIT_TIMEOUT_MS     K_SECONDS(30)
#define REGISTRATION_TIMEOUT_MS     K_SECONDS(10)
#define MQTT_CONNECT_TIMEOUT_MS     K_SECONDS(10)
#define RETRY_DELAY_MS              K_SECONDS(5)
#define MAX_RETRY_COUNT             3

/* Forward declarations */
static void state_transition(init_sm_context_t *ctx, init_state_t new_state);
static void process_event(init_event_t event);

/* State handlers */
static void state_init_handler(init_sm_context_t *ctx, init_event_t event);
static void state_wait_network_handler(init_sm_context_t *ctx, init_event_t event);
static void state_network_ready_handler(init_sm_context_t *ctx, init_event_t event);
static void state_device_reg_start_handler(init_sm_context_t *ctx, init_event_t event);
static void state_device_reg_in_progress_handler(init_sm_context_t *ctx, init_event_t event);
static void state_device_reg_complete_handler(init_sm_context_t *ctx, init_event_t event);
static void state_mqtt_init_start_handler(init_sm_context_t *ctx, init_event_t event);
static void state_mqtt_connecting_handler(init_sm_context_t *ctx, init_event_t event);
static void state_operational_handler(init_sm_context_t *ctx, init_event_t event);
static void state_error_handler(init_sm_context_t *ctx, init_event_t event);

/* State entry/exit handlers */
static void state_init_entry(init_sm_context_t *ctx, init_event_t event);
static void state_wait_network_entry(init_sm_context_t *ctx, init_event_t event);
static void state_network_ready_entry(init_sm_context_t *ctx, init_event_t event);
static void state_device_reg_start_entry(init_sm_context_t *ctx, init_event_t event);
static void state_device_reg_in_progress_entry(init_sm_context_t *ctx, init_event_t event);
static void state_device_reg_complete_entry(init_sm_context_t *ctx, init_event_t event);
static void state_mqtt_init_start_entry(init_sm_context_t *ctx, init_event_t event);
static void state_mqtt_connecting_entry(init_sm_context_t *ctx, init_event_t event);
static void state_operational_entry(init_sm_context_t *ctx, init_event_t event);
static void state_error_entry(init_sm_context_t *ctx, init_event_t event);

/* Common exit handler */
static void state_common_exit(init_sm_context_t *ctx, init_event_t event);

/* State table */
static const state_info_t state_table[STATE_MAX] = {
    [STATE_INIT] = {
        .name = "INIT",
        .entry = state_init_entry,
        .exit = state_common_exit,
        .handler = state_init_handler
    },
    [STATE_WAIT_NETWORK] = {
        .name = "WAIT_NETWORK",
        .entry = state_wait_network_entry,
        .exit = state_common_exit,
        .handler = state_wait_network_handler
    },
    [STATE_NETWORK_READY] = {
        .name = "NETWORK_READY",
        .entry = state_network_ready_entry,
        .exit = state_common_exit,
        .handler = state_network_ready_handler
    },
    [STATE_DEVICE_REG_START] = {
        .name = "DEVICE_REG_START",
        .entry = state_device_reg_start_entry,
        .exit = state_common_exit,
        .handler = state_device_reg_start_handler
    },
    [STATE_DEVICE_REG_IN_PROGRESS] = {
        .name = "DEVICE_REG_IN_PROGRESS",
        .entry = state_device_reg_in_progress_entry,
        .exit = state_common_exit,
        .handler = state_device_reg_in_progress_handler
    },
    [STATE_DEVICE_REG_COMPLETE] = {
        .name = "DEVICE_REG_COMPLETE",
        .entry = state_device_reg_complete_entry,
        .exit = state_common_exit,
        .handler = state_device_reg_complete_handler
    },
    [STATE_MQTT_INIT_START] = {
        .name = "MQTT_INIT_START",
        .entry = state_mqtt_init_start_entry,
        .exit = state_common_exit,
        .handler = state_mqtt_init_start_handler
    },
    [STATE_MQTT_CONNECTING] = {
        .name = "MQTT_CONNECTING",
        .entry = state_mqtt_connecting_entry,
        .exit = state_common_exit,
        .handler = state_mqtt_connecting_handler
    },
    [STATE_OPERATIONAL] = {
        .name = "OPERATIONAL",
        .entry = state_operational_entry,
        .exit = state_common_exit,
        .handler = state_operational_handler
    },
    [STATE_ERROR] = {
        .name = "ERROR",
        .entry = state_error_entry,
        .exit = state_common_exit,
        .handler = state_error_handler
    }
};

/* Global context */
static init_sm_context_t sm_context;
static struct k_mutex sm_mutex;

/* Network event callback */
static struct net_mgmt_event_callback net_mgmt_cb;

/* Work handlers */
static void timeout_work_handler(struct k_work *work)
{
    LOG_WRN("Operation timeout in state %s", state_table[sm_context.current_state].name);
    process_event(EVENT_TIMEOUT);
}

static void retry_work_handler(struct k_work *work)
{
    LOG_INF("Retry work handler called, current state: %s (%d)", 
            state_table[sm_context.current_state].name, sm_context.current_state);
    process_event(EVENT_RETRY);
}

/* Network event handler */
static void net_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                   uint32_t mgmt_event,
                                   struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_L4_CONNECTED) {
        LOG_INF("Network connected event received");
        init_state_machine_send_event(EVENT_NETWORK_UP);
    } else if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
        LOG_WRN("Network disconnected event received");
        init_state_machine_send_event(EVENT_NETWORK_DOWN);
    }
}

#ifdef CONFIG_RPR_MODULE_MQTT
/* MQTT event handler callback */
static void mqtt_event_callback(mqtt_event_type_t event_type)
{
    switch (event_type) {
    case MQTT_EVENT_CONNECTED:
        LOG_INF("MQTT event callback: Connected");
        init_state_machine_send_event(EVENT_MQTT_CONNECTED);
        break;
    case MQTT_EVENT_DISCONNECTED:
        LOG_INF("MQTT event callback: Disconnected");
        init_state_machine_send_event(EVENT_MQTT_DISCONNECTED);
        break;
    case MQTT_EVENT_CONNECT_FAILED:
        LOG_INF("MQTT event callback: Connect failed");
        init_state_machine_send_event(EVENT_MQTT_FAILURE);
        break;
    }
}
#endif

/* State transition logic */
static void state_transition(init_sm_context_t *ctx, init_state_t new_state)
{
    if (new_state >= STATE_MAX) {
        LOG_ERR("Invalid state transition to %d", new_state);
        return;
    }

    if (ctx->current_state == new_state) {
        return; /* No transition needed */
    }

    LOG_INF("State transition: %s -> %s",
            state_table[ctx->current_state].name,
            state_table[new_state].name);

    /* Call exit handler of current state */
    if (state_table[ctx->current_state].exit) {
        state_table[ctx->current_state].exit(ctx, EVENT_MAX);
    }

    /* Update state */
    ctx->previous_state = ctx->current_state;
    ctx->current_state = new_state;

    /* Call entry handler of new state */
    if (state_table[new_state].entry) {
        state_table[new_state].entry(ctx, EVENT_MAX);
    }
}

/* Process event in current state */
static void process_event(init_event_t event)
{
    k_mutex_lock(&sm_mutex, K_FOREVER);

    if (event >= EVENT_MAX) {
        LOG_ERR("Invalid event %d", event);
        k_mutex_unlock(&sm_mutex);
        return;
    }

    LOG_DBG("Processing event %d in state %s", event, 
            state_table[sm_context.current_state].name);

    /* Call current state's handler */
    if (state_table[sm_context.current_state].handler) {
        state_table[sm_context.current_state].handler(&sm_context, event);
    }

    k_mutex_unlock(&sm_mutex);
}

/* State entry handlers */
static void state_init_entry(init_sm_context_t *ctx, init_event_t event)
{
    LOG_INF("Entering INIT state");
    ctx->retry_count = 0;
    ctx->network_available = false;
    ctx->device_registered = false;
    ctx->mqtt_connected = false;
}

static void state_wait_network_entry(init_sm_context_t *ctx, init_event_t event)
{
    LOG_INF("Entering WAIT_NETWORK state");
    
    /* Check if network is already up */
    struct net_if *iface = net_if_get_default();
    if (iface && net_if_is_up(iface)) {
        LOG_INF("Network already available");
        ctx->network_available = true;
        process_event(EVENT_NETWORK_UP);
    } else {
        /* Start timeout timer */
        k_work_schedule(&ctx->timeout_work, NETWORK_WAIT_TIMEOUT_MS);
    }
}

static void state_network_ready_entry(init_sm_context_t *ctx, init_event_t event)
{
    LOG_INF("Entering NETWORK_READY state - network is available");
    ctx->network_available = true;
    
    /* Automatically proceed to next state */
#ifdef CONFIG_RPR_MODULE_DEVICE_REGISTRY
    state_transition(ctx, STATE_DEVICE_REG_START);
#else
    /* Skip device registration if not configured */
    LOG_INF("Device registry not enabled, skipping to MQTT");
    state_transition(ctx, STATE_MQTT_INIT_START);
#endif
}

static void state_device_reg_start_entry(init_sm_context_t *ctx, init_event_t event)
{
    LOG_INF("Entering DEVICE_REG_START state");
    ctx->retry_count = 0;
    
    /* Schedule the registration process to run after state machine settles */
    k_work_schedule(&ctx->retry_work, K_MSEC(10));
}

static void state_device_reg_in_progress_entry(init_sm_context_t *ctx, init_event_t event)
{
    LOG_INF("Entering DEVICE_REG_IN_PROGRESS state");
    k_work_schedule(&ctx->timeout_work, REGISTRATION_TIMEOUT_MS);
}

static void state_device_reg_complete_entry(init_sm_context_t *ctx, init_event_t event)
{
    LOG_INF("Entering DEVICE_REG_COMPLETE state, device ID: %s", ctx->device_id);
    /* This is a transient state - schedule immediate transition */
    k_work_schedule(&ctx->retry_work, K_MSEC(1));
}

static void state_mqtt_init_start_entry(init_sm_context_t *ctx, init_event_t event)
{
    LOG_INF("Entering MQTT_INIT_START state");
    ctx->retry_count = 0;
    
    /* Schedule the initialization to run after state machine settles */
    LOG_INF("Scheduling retry work for MQTT initialization in 100ms");
    int ret = k_work_schedule(&ctx->retry_work, K_MSEC(100));
    if (ret < 0) {
        LOG_ERR("Failed to schedule retry work: %d", ret);
    } else {
        LOG_INF("Retry work scheduled successfully (ret=%d)", ret);
    }
}

static void state_mqtt_connecting_entry(init_sm_context_t *ctx, init_event_t event)
{
    LOG_INF("Entering MQTT_CONNECTING state");
    k_work_schedule(&ctx->timeout_work, MQTT_CONNECT_TIMEOUT_MS);
}

static void state_operational_entry(init_sm_context_t *ctx, init_event_t event)
{
    LOG_INF("Entering OPERATIONAL state - system fully initialized!");
    LOG_INF("Device ID: %s", ctx->device_id);
    
#ifdef CONFIG_RPR_MODULE_MQTT
    /* Initialize MQTT audio handler */
    int ret = mqtt_audio_handler_init();
    if (ret == 0) {
        LOG_INF("MQTT audio handler initialized successfully");
    } else {
        LOG_ERR("Failed to initialize MQTT audio handler: %d", ret);
    }
    
    /* Initialize MQTT log client if enabled */
#ifdef CONFIG_RPR_MQTT_LOG_CLIENT
    ret = mqtt_log_client_init();
    if (ret == 0) {
        LOG_INF("MQTT log client initialized successfully");
    } else {
        LOG_ERR("Failed to initialize MQTT log client: %d", ret);
    }
#endif
#endif
}

static void state_error_entry(init_sm_context_t *ctx, init_event_t event)
{
    LOG_ERR("Entering ERROR state with error code: %d", ctx->error_code);
    
    if (ctx->retry_count < MAX_RETRY_COUNT) {
        LOG_INF("Scheduling retry %d/%d", ctx->retry_count + 1, MAX_RETRY_COUNT);
        k_work_schedule(&ctx->retry_work, RETRY_DELAY_MS);
    } else {
        LOG_ERR("Max retries exceeded, system initialization failed");
    }
}

static void state_common_exit(init_sm_context_t *ctx, init_event_t event)
{
    LOG_DBG("Exiting %s state", state_table[ctx->current_state].name);
    
    /* Cancel any pending work */
    k_work_cancel_delayable(&ctx->timeout_work);
    k_work_cancel_delayable(&ctx->retry_work);
}

/* State handlers */
static void state_init_handler(init_sm_context_t *ctx, init_event_t event)
{
    switch (event) {
    case EVENT_START:
        state_transition(ctx, STATE_WAIT_NETWORK);
        break;
    default:
        LOG_WRN("Unexpected event %d in INIT state", event);
        break;
    }
}

static void state_wait_network_handler(init_sm_context_t *ctx, init_event_t event)
{
    switch (event) {
    case EVENT_NETWORK_UP:
        state_transition(ctx, STATE_NETWORK_READY);
        break;
    case EVENT_TIMEOUT:
        LOG_ERR("Network wait timeout");
        ctx->error_code = -ETIMEDOUT;
        state_transition(ctx, STATE_ERROR);
        break;
    default:
        LOG_WRN("Unexpected event %d in WAIT_NETWORK state", event);
        break;
    }
}

static void state_network_ready_handler(init_sm_context_t *ctx, init_event_t event)
{
    /* Network ready state automatically transitions in entry handler */
    switch (event) {
    case EVENT_NETWORK_DOWN:
        LOG_WRN("Network lost in NETWORK_READY state");
        state_transition(ctx, STATE_WAIT_NETWORK);
        break;
    default:
        LOG_DBG("Event %d in NETWORK_READY state", event);
        break;
    }
}

static void state_device_reg_start_handler(init_sm_context_t *ctx, init_event_t event)
{
#ifdef CONFIG_RPR_MODULE_DEVICE_REGISTRY
    device_registry_result_t reg_result;
    int ret;
    
    /* Only process on retry event */
    if (event != EVENT_RETRY && event != EVENT_TIMEOUT) {
        LOG_DBG("Ignoring event %d in DEVICE_REG_START state", event);
        return;
    }

    LOG_INF("Starting device registration");
    
    /* Get full device ID */
    size_t device_id_len;
    const char *full_device_id = dev_info_get_device_id_str(&device_id_len);
    
    ret = device_registry_register(
        full_device_id,
        CONFIG_RPR_DEVICE_REGISTRY_PREFERRED_ID_LENGTH,
        CONFIG_RPR_DEVICE_REGISTRY_GITHUB_TOKEN,
        CONFIG_RPR_DEVICE_REGISTRY_REPO_OWNER,
        CONFIG_RPR_DEVICE_REGISTRY_REPO_NAME,
        &reg_result
    );

    if (ret == 0 || ret == -EALREADY) {
        /* Success or already registered */
        strncpy(ctx->device_id, reg_result.assigned_id, sizeof(ctx->device_id) - 1);
        ctx->device_registered = true;
        state_transition(ctx, STATE_DEVICE_REG_COMPLETE);
    } else if (ret == -EINPROGRESS) {
        /* Registration in progress */
        state_transition(ctx, STATE_DEVICE_REG_IN_PROGRESS);
    } else {
        /* Registration failed, use fallback */
        LOG_WRN("Registration failed (%d), using fallback ID", ret);
        strncpy(ctx->device_id, full_device_id, 
                MIN(CONFIG_RPR_DEVICE_REGISTRY_PREFERRED_ID_LENGTH, sizeof(ctx->device_id) - 1));
        ctx->device_registered = false;
        state_transition(ctx, STATE_DEVICE_REG_COMPLETE);
    }
#else
    state_transition(ctx, STATE_MQTT_INIT_START);
#endif
}

static void state_device_reg_in_progress_handler(init_sm_context_t *ctx, init_event_t event)
{
    switch (event) {
    case EVENT_REG_SUCCESS:
        ctx->device_registered = true;
        state_transition(ctx, STATE_DEVICE_REG_COMPLETE);
        break;
    case EVENT_REG_FAILURE:
        /* Use fallback ID */
        ctx->device_registered = false;
        state_transition(ctx, STATE_DEVICE_REG_COMPLETE);
        break;
    case EVENT_TIMEOUT:
        LOG_WRN("Device registration timeout");
        ctx->retry_count++;
        state_transition(ctx, STATE_ERROR);
        break;
    case EVENT_NETWORK_DOWN:
        LOG_WRN("Network lost during registration");
        state_transition(ctx, STATE_WAIT_NETWORK);
        break;
    default:
        LOG_WRN("Unexpected event %d in DEVICE_REG_IN_PROGRESS state", event);
        break;
    }
}

static void state_device_reg_complete_handler(init_sm_context_t *ctx, init_event_t event)
{
    /* This is a transient state - transition on the first event we receive */
    static bool transitioning = false;
    
    if (!transitioning) {
        transitioning = true;
        LOG_INF("DEVICE_REG_COMPLETE is transient, transitioning to MQTT_INIT_START on event %d", event);
        state_transition(ctx, STATE_MQTT_INIT_START);
        transitioning = false;
    } else {
        LOG_DBG("Ignoring event %d during transition", event);
    }
}

static void state_mqtt_init_start_handler(init_sm_context_t *ctx, init_event_t event)
{
#ifdef CONFIG_RPR_MODULE_MQTT
    int ret;

    /* Handle MQTT connected event if it arrives quickly */
    if (event == EVENT_MQTT_CONNECTED) {
        LOG_INF("MQTT connected quickly, transitioning to operational state");
        state_transition(ctx, STATE_OPERATIONAL);
        return;
    }
    
    /* Only process initialization on retry event or timeout */
    if (event != EVENT_RETRY && event != EVENT_TIMEOUT) {
        LOG_DBG("Ignoring event %d in MQTT_INIT_START state", event);
        return;
    }
    
    LOG_INF("Processing %s event in MQTT_INIT_START state", 
            event == EVENT_RETRY ? "RETRY" : "TIMEOUT");

    LOG_INF("Starting MQTT initialization with device ID: %s", ctx->device_id);
    
    /* Initialize MQTT with the assigned device ID */
    ret = mqtt_init();
    if (ret == 0 || ret == -EALREADY) {
        if (ret == -EALREADY) {
            LOG_WRN("MQTT module already initialized");
        }
        /* Check if already connected */
        if (mqtt_is_connected()) {
            LOG_INF("MQTT already connected");
            state_transition(ctx, STATE_OPERATIONAL);
        } else {
            ret = mqtt_module_connect();
            if (ret == 0) {
                state_transition(ctx, STATE_MQTT_CONNECTING);
            } else if (ret == -EALREADY) {
                LOG_INF("MQTT connection already in progress");
                state_transition(ctx, STATE_MQTT_CONNECTING);
            } else {
                LOG_ERR("MQTT connect failed: %d", ret);
                ctx->error_code = ret;
                ctx->retry_count++;
                state_transition(ctx, STATE_ERROR);
            }
        }
    } else {
        LOG_ERR("MQTT init failed: %d", ret);
        ctx->error_code = ret;
        state_transition(ctx, STATE_ERROR);
    }
#else
    /* No MQTT configured, go directly to operational */
    state_transition(ctx, STATE_OPERATIONAL);
#endif
}

static void state_mqtt_connecting_handler(init_sm_context_t *ctx, init_event_t event)
{
    switch (event) {
    case EVENT_MQTT_CONNECTED:
        LOG_INF("MQTT connected successfully");
        ctx->mqtt_connected = true;
        
#ifdef CONFIG_RPR_MODULE_MQTT
        /* Start heartbeat if configured */
        mqtt_start_heartbeat();
#endif
        state_transition(ctx, STATE_OPERATIONAL);
        break;
    case EVENT_MQTT_FAILURE:
    case EVENT_TIMEOUT:
        LOG_ERR("MQTT connection failed");
        ctx->retry_count++;
        state_transition(ctx, STATE_ERROR);
        break;
    case EVENT_NETWORK_DOWN:
        LOG_WRN("Network lost during MQTT connection");
        state_transition(ctx, STATE_WAIT_NETWORK);
        break;
    default:
        LOG_WRN("Unexpected event %d in MQTT_CONNECTING state", event);
        break;
    }
}

static void state_operational_handler(init_sm_context_t *ctx, init_event_t event)
{
    switch (event) {
    case EVENT_NETWORK_DOWN:
        LOG_WRN("Network lost in operational state");
        ctx->network_available = false;
        state_transition(ctx, STATE_WAIT_NETWORK);
        break;
    case EVENT_MQTT_DISCONNECTED:
        LOG_WRN("MQTT disconnected in operational state");
        ctx->mqtt_connected = false;
        state_transition(ctx, STATE_MQTT_INIT_START);
        break;
    default:
        LOG_DBG("Event %d in OPERATIONAL state", event);
        break;
    }
}

static void state_error_handler(init_sm_context_t *ctx, init_event_t event)
{
    switch (event) {
    case EVENT_RETRY:
        LOG_INF("Retrying from error state");
        
        /* Determine where to retry based on what failed */
        if (!ctx->network_available) {
            state_transition(ctx, STATE_WAIT_NETWORK);
        } else if (!ctx->device_registered && ctx->previous_state <= STATE_DEVICE_REG_IN_PROGRESS) {
            state_transition(ctx, STATE_DEVICE_REG_START);
        } else if (!ctx->mqtt_connected) {
            state_transition(ctx, STATE_MQTT_INIT_START);
        } else {
            LOG_ERR("Unable to determine retry state");
        }
        break;
    case EVENT_NETWORK_UP:
        LOG_INF("Network restored in error state");
        ctx->network_available = true;
        /* Retry will be triggered by retry timer */
        break;
    default:
        LOG_WRN("Event %d in ERROR state", event);
        break;
    }
}

/* Public API implementation */
int init_state_machine_init(void)
{
    LOG_INF("Initializing device initialization state machine");
    
    /* Initialize mutex */
    k_mutex_init(&sm_mutex);
    
    /* Initialize context */
    memset(&sm_context, 0, sizeof(sm_context));
    sm_context.current_state = STATE_INIT;
    sm_context.previous_state = STATE_INIT;
    
    /* Initialize work items */
    k_work_init_delayable(&sm_context.timeout_work, timeout_work_handler);
    k_work_init_delayable(&sm_context.retry_work, retry_work_handler);
    
    /* Register network event callback */
    net_mgmt_init_event_callback(&net_mgmt_cb,
                                 net_mgmt_event_handler,
                                 NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED);
    net_mgmt_add_event_callback(&net_mgmt_cb);
    
#ifdef CONFIG_RPR_MODULE_MQTT
    /* Register MQTT event callback */
    mqtt_set_event_handler(mqtt_event_callback);
#endif
    
    LOG_DBG("State machine initialized");
    return 0;
}

void init_state_machine_start(void)
{
    LOG_INF("Starting device initialization state machine");
    process_event(EVENT_START);
}

void init_state_machine_send_event(init_event_t event)
{
    process_event(event);
}

init_state_t init_state_machine_get_state(void)
{
    init_state_t state;
    k_mutex_lock(&sm_mutex, K_FOREVER);
    state = sm_context.current_state;
    k_mutex_unlock(&sm_mutex);
    return state;
}

bool init_state_machine_is_operational(void)
{
    return init_state_machine_get_state() == STATE_OPERATIONAL;
}

const char *init_state_machine_get_device_id(void)
{
    return sm_context.device_id;
}
