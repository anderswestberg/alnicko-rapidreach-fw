/*
 * Copyright (c) 2025 Alnicko Lab OU. All rights reserved.
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/dhcpv4.h>

#ifdef CONFIG_EXAMPLES_ENABLE_MAIN_EXAMPLES
#include "domain_logic.h"
#endif

#ifdef CONFIG_RPR_MODULE_MQTT
#include "../mqtt_module/mqtt_module.h"
/* Old audio handler removed - using mqtt_audio_handler_v2 via mqtt_wrapper */
#endif
#ifdef CONFIG_RPR_MQTT_LOG_CLIENT
#include "../mqtt_log_client/mqtt_log_client.h"
#endif
#ifdef CONFIG_RPR_MODULE_DEVICE_REGISTRY
#include "../device_registry/device_registry.h"
#endif
#ifdef CONFIG_RPR_MODULE_INIT_SM
#include "../init_state_machine/init_state_machine.h"
#endif
#include "shell_keepalive.h"

#ifdef CONFIG_EXAMPLES_ENABLE_MAIN_EXAMPLES
#include "../examples/power_supervisor.h"
#endif

#ifdef CONFIG_LOG_SETTINGS
#include "../log_settings/log_settings.h"
#endif

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Track link status to trigger re-init of shell backend if needed */
static atomic_t link_up = ATOMIC_INIT(0);

/* Simple ping callback for watchdog until domain logic initializes */
static bool early_ping_callback(void)
{
    return true; /* Always return true during early boot */
}

static void net_event_handler(struct net_mgmt_event_callback *cb,
                              uint32_t mgmt_event, struct net_if *iface)
{
#ifdef CONFIG_SHELL_BACKEND_MQTT
    if (mgmt_event == NET_EVENT_L4_CONNECTED) {
        atomic_set(&link_up, 1);
        LOG_INF("Network L4 connected - MQTT shell backend should reconnect automatically");
    } else if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
        atomic_set(&link_up, 0);
        LOG_WRN("Network L4 disconnected - MQTT shell backend will disconnect");
    }
#endif
}

static struct net_mgmt_event_callback net_cb;

/* Network startup handler - ensures DHCP and MQTT are ready */
static void network_startup_handler(void)
{
    LOG_INF("Starting network initialization...");
    
    /* Get the default network interface */
    struct net_if *iface = net_if_get_default();
    if (!iface) {
        LOG_ERR("No default network interface found");
        return;
    }
    
    /* Register for network events so we know when link comes back */
    net_mgmt_init_event_callback(&net_cb, net_event_handler,
        NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED);
    net_mgmt_add_event_callback(&net_cb);

    /* Start DHCP if enabled */
#ifdef CONFIG_NET_DHCPV4
    LOG_INF("Starting DHCP client...");
    net_dhcpv4_start(iface);
    
    /* Wait for DHCP to get an IP address (with timeout) */
    int retry = 0;
    while (retry < 30) { /* 30 seconds timeout */
        if (net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED)) {
            LOG_INF("DHCP successful, got IP address");
            break;
        }
        k_sleep(K_SECONDS(1));
        retry++;
    }
    
    if (retry >= 30) {
        LOG_WRN("DHCP timeout - continuing anyway");
    }
#endif

    /* Give the MQTT shell backend time to connect first */
#ifdef CONFIG_SHELL_BACKEND_MQTT
    LOG_INF("Waiting for MQTT shell backend to connect...");
    /* The MQTT shell backend should connect automatically when it receives 
     * the NET_EVENT_L4_CONNECTED event. Give it time to complete
     * DNS resolution and connection before starting other MQTT clients */
    k_sleep(K_SECONDS(15));

    /* If link was down at boot and later comes back, we can optionally
     * wait until NET_EVENT_L4_CONNECTED before starting main MQTT */
    if (!atomic_get(&link_up)) {
        LOG_WRN("Link not up yet; delaying main MQTT start until connected");
        /* Busy-wait up to 30s for link */
        for (int i = 0; i < 30; i++) {
            if (atomic_get(&link_up)) break;
            k_sleep(K_SECONDS(1));
        }
    }
    
    /* Start MQTT shell keepalive to prevent 90-second timeout */
    LOG_INF("Starting MQTT shell keepalive");
    mqtt_shell_keepalive_start();
#endif

    /* MQTT module initialization is handled by the state machine when enabled */
#ifdef CONFIG_RPR_MODULE_MQTT
#ifndef CONFIG_RPR_INIT_STATE_MACHINE
    /* Only connect directly if state machine is not handling it */
    LOG_INF("Connecting MQTT client...");
    mqtt_status_t status = mqtt_module_connect();
    if (status == MQTT_SUCCESS) {
        LOG_INF("MQTT connected successfully");
        
        /* Optionally start heartbeat */
        mqtt_start_heartbeat();
        
        /* Audio handler initialization now done by init state machine via mqtt_wrapper */
        /* Old mqtt_audio_handler.c has been removed - using mqtt_audio_handler_v2.c */
    } else {
        LOG_WRN("MQTT connection failed: %d", status);
        /* Auto-reconnect will handle retries */
    }
#endif /* !CONFIG_RPR_INIT_STATE_MACHINE */
#endif /* CONFIG_RPR_MODULE_MQTT */

    /* Initialize MQTT log client after MQTT main connects */
#ifdef CONFIG_RPR_MQTT_LOG_CLIENT
#ifndef CONFIG_RPR_INIT_STATE_MACHINE
    (void)mqtt_log_client_init();
#endif
#endif
}

/* Network startup thread resources */
K_THREAD_STACK_DEFINE(network_startup_stack, 2048);
static struct k_thread network_startup_thread;

int main(void)
{
    int ret;
    
#ifdef CONFIG_EXAMPLES_ENABLE_MAIN_EXAMPLES
    /* Register early ping callback to prevent watchdog resets during boot */
    supervisor_ping_register_callback(early_ping_callback);
    LOG_INF("Registered early ping callback for watchdog");
#endif

#ifdef CONFIG_LOG_SETTINGS_AUTO_LOAD
    /* Load saved log settings early in boot */
    ret = log_settings_init();
    if (ret < 0) {
        LOG_WRN("Failed to initialize log settings: %d", ret);
    }
#endif

#ifdef CONFIG_RPR_MODULE_INIT_SM
    /* Initialize and start the state machine for system startup */
    ret = init_state_machine_init();
    if (ret < 0) {
        LOG_ERR("Failed to initialize state machine: %d", ret);
        /* Fall back to sequential startup */
        k_thread_create(&network_startup_thread, 
                        network_startup_stack,
                        K_THREAD_STACK_SIZEOF(network_startup_stack),
                        (k_thread_entry_t)network_startup_handler,
                        NULL, NULL, NULL,
                        K_PRIO_COOP(5), 0, K_NO_WAIT);
        k_thread_name_set(&network_startup_thread, "network_startup");
    } else {
        /* Start the state machine */
        init_state_machine_start();
        
        /* Wait for the state machine to reach operational state */
        LOG_INF("Waiting for system to become operational...");
        while (!init_state_machine_is_operational()) {
            k_sleep(K_SECONDS(1));
        }
        LOG_INF("System is operational!");
    }
    
#else
    /* Use the old sequential startup if state machine is not enabled */
    k_thread_create(&network_startup_thread, 
                    network_startup_stack,
                    K_THREAD_STACK_SIZEOF(network_startup_stack),
                    (k_thread_entry_t)network_startup_handler,
                    NULL, NULL, NULL,
                    K_PRIO_COOP(5), 0, K_NO_WAIT);
    k_thread_name_set(&network_startup_thread, "network_startup");
#endif

#ifdef CONFIG_EXAMPLES_ENABLE_MAIN_EXAMPLES
    domain_logic_func();
#endif

    return 0;
}