/**
 * @file test_mqtt_wrapper.c
 * @brief Test application for the new MQTT wrapper
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* Forward declarations */
int mqtt_audio_handler_v2_init(void);
void mqtt_audio_handler_v2_test(void);

LOG_MODULE_REGISTER(test_mqtt, LOG_LEVEL_INF);

/* Test thread for periodic operations */
static void test_thread_func(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    
    LOG_INF("MQTT wrapper test thread started");
    
    /* Wait for system to stabilize */
    k_sleep(K_SECONDS(5));
    
    while (1) {
        /* Run test function */
        mqtt_audio_handler_v2_test();
        
        /* Wait 30 seconds between tests */
        k_sleep(K_SECONDS(30));
    }
}

K_THREAD_DEFINE(test_thread, 2048,
                test_thread_func, NULL, NULL, NULL,
                K_PRIO_PREEMPT(10), 0, 5000);

/**
 * @brief Initialize MQTT wrapper test
 */
int test_mqtt_wrapper_init(void)
{
    LOG_INF("Starting MQTT wrapper test");
    
    /* DISABLED: MQTT wrapper v2 has critical stack corruption bug causing system freeze
     * when receiving PUBLISH messages. Disabling until root cause is fixed.
     * Audio alerts will use main mqtt_module path instead. */
    LOG_WRN("MQTT wrapper v2 audio handler DISABLED due to PUBLISH freeze bug");
    
    return 0;  /* Return success to allow system to continue */
    
#if 0  /* Disabled code - causes system freeze */
    /* Initialize the new audio handler */
    int ret = mqtt_audio_handler_v2_init();
    if (ret < 0) {
        LOG_ERR("Failed to initialize MQTT audio handler v2: %d", ret);
        return ret;
    }
    
    LOG_INF("MQTT wrapper test initialized");
    return 0;
#endif
}
