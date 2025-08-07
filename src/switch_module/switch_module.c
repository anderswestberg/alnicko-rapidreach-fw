/**
 * @file switch_module.c
 * @brief Hardware switch input module with debounce and switch callbacks.
 *
 * This module initializes 4 GPIO switches, configures
 * edge-triggered interrupts for them, and applies software debounce
 * using delayed work. Each switch can register its own user-defined
 * callback function to handle input events.
 *
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "switch_module.h"

LOG_MODULE_REGISTER(switch_module, CONFIG_RPR_MODULE_SWITCH_LOG_LEVEL);

#define SWITCH_COUNT      4
#define DEBOUNCE_DELAY_MS 100

#define SWITCH0_NODE DT_ALIAS(switch0)
#define SWITCH1_NODE DT_ALIAS(switch1)
#define SWITCH2_NODE DT_ALIAS(switch2)
#define SWITCH3_NODE DT_ALIAS(switch3)

#define SWITCH0_SPEC GPIO_DT_SPEC_GET(SWITCH0_NODE, gpios)
#define SWITCH1_SPEC GPIO_DT_SPEC_GET(SWITCH1_NODE, gpios)
#define SWITCH2_SPEC GPIO_DT_SPEC_GET(SWITCH2_NODE, gpios)
#define SWITCH3_SPEC GPIO_DT_SPEC_GET(SWITCH3_NODE, gpios)

struct switch_module_config {
    struct gpio_dt_spec     switch_pins[SWITCH_COUNT];
    struct gpio_callback    callbacks[SWITCH_COUNT];
    switch_user_callback_t  user_callbacks[SWITCH_COUNT];
    struct k_work_delayable debounce_works[SWITCH_COUNT];
    bool                    is_ready;
};

 static struct switch_module_config sw_cfg = {
     .switch_pins = {
         SWITCH0_SPEC,
         SWITCH1_SPEC,
         SWITCH2_SPEC,
         SWITCH3_SPEC,
     },
     .user_callbacks = {NULL},
     .is_ready = false,
 };
 

 /**
 * @brief Register a callback for a specific switch.
 *
 * @param index Switch index [0..3]
 * @param cb    Callback function to call on switch event
 */
void switch_register_callback(uint8_t index, switch_user_callback_t cb)
{
    if (index < SWITCH_COUNT) {
        sw_cfg.user_callbacks[index] = cb;
    }
}

// Generate switches interrupt handlers using a macro
#define DEFINE_SWITCH_IRQ_HANDLER(n)                             \
    static void switch##n##_callback(const struct device  *port, \
                                     struct gpio_callback *cb,   \
                                     uint32_t              pins) \
    {                                                            \
        k_work_reschedule(&sw_cfg.debounce_works[n],             \
                          K_MSEC(DEBOUNCE_DELAY_MS));            \
    }

DEFINE_SWITCH_IRQ_HANDLER(0)
DEFINE_SWITCH_IRQ_HANDLER(1)
DEFINE_SWITCH_IRQ_HANDLER(2)
DEFINE_SWITCH_IRQ_HANDLER(3)

/**
 * @brief Debounce handler for delayed execution.
 *
 * Called after debounce delay; invokes the user-registered callback
 * for the switch that triggered.
 *
 * @param work Pointer to the delayed work item.
 */
static void debounce_handler(struct k_work *work)
{
    int                      index = -1;
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);

    for (int i = 0; i < SWITCH_COUNT; i++) {
        if (dwork == &sw_cfg.debounce_works[i]) {
            index = i;
            break;
        }
    }

    if (index == -1) {
        LOG_ERR("Unknown work item");
        return;
    }

    LOG_DBG("Switch %d debounced", index);

    if (sw_cfg.user_callbacks[index]) {
        sw_cfg.user_callbacks[index]();
    }
}

/**
  * @brief Initialize switch GPIOs and interrupt handlers.
  *
  * @return 0 on success, negative error code otherwise.
  */
static int switch_module_init(void)
{
    static gpio_callback_handler_t cb_funcs[SWITCH_COUNT] = {
        switch0_callback,
        switch1_callback,
        switch2_callback,
        switch3_callback,
    };

    for (int i = 0; i < SWITCH_COUNT; i++) {
        if (!device_is_ready(sw_cfg.switch_pins[i].port)) {
            LOG_ERR("Switch GPIO %d not ready", i);
            return -ENODEV;
        }

        int ret = gpio_pin_configure_dt(&sw_cfg.switch_pins[i], GPIO_INPUT);
        if (ret < 0) {
            LOG_ERR("Failed to configure switch GPIO %d (err %d)", i, ret);
            return ret;
        }

        ret = gpio_pin_interrupt_configure_dt(&sw_cfg.switch_pins[i], GPIO_INT_EDGE_BOTH);
        if (ret < 0) {
            LOG_ERR("Failed to configure interrupt for switch %d (err %d)", i, ret);
            return ret;
        }

        gpio_init_callback(&sw_cfg.callbacks[i],
                           cb_funcs[i],
                           BIT(sw_cfg.switch_pins[i].pin));
        gpio_add_callback(sw_cfg.switch_pins[i].port, &sw_cfg.callbacks[i]);
        k_work_init_delayable(&sw_cfg.debounce_works[i], debounce_handler);
    }

     sw_cfg.is_ready = true;
     LOG_DBG("All switch GPIOs initialized");
     return 0;
}

 SYS_INIT(switch_module_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
 
 /**
  * @brief Get the state of a specific switch.
  *
  * @param index Switch index [0..3]
  * @return 1 if HIGH, 0 if LOW, negative error code on failure.
  */
 int switch_get_state(uint8_t index)
 {
     if (!sw_cfg.is_ready) {
         LOG_ERR("Switch module not initialized");
         return -ENODEV;
     }
 
     if (index >= SWITCH_COUNT) {
         LOG_ERR("Invalid switch index: %d", index);
         return -EINVAL;
     }
 
     int val = gpio_pin_get_dt(&sw_cfg.switch_pins[index]);
     if (val < 0) {
         LOG_ERR("Failed to read switch %d (err %d)", index, val);
     }
 
     return val;
 }
