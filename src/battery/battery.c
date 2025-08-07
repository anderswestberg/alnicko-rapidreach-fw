/**
 * @file battery.c
 * @brief Battery voltage monitoring module using ADC and optional voltage divider.
 *
 * This module provides battery voltage measurement and estimation functionality for embedded systems
 * based on the Zephyr RTOS. It uses the Analog-to-Digital Converter (ADC) to sample battery voltage
 * directly or through a resistive divider, depending on devicetree configuration.
 *
 * Based on Zephyr’s "Battery Voltage Measurement" sample.
 *
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include "battery.h"

LOG_MODULE_REGISTER(battery, CONFIG_RPR_MODULE_BATTERY_LOG_LEVEL);

#define VBATT DT_PATH(vbatt)

#define BATTERY_ADC_GAIN ADC_GAIN_1

static const struct battery_level_point levels[] = {
    { 10000, 16800 }, { 9500, 16600 }, { 9000, 16400 }, { 8500, 16150 },
    { 8000, 16050 },  { 7500, 15700 }, { 7000, 15400 }, { 6500, 15200 },
    { 6000, 15100 },  { 5500, 15000 }, { 5000, 14800 }, { 4500, 14700 },
    { 4000, 14600 },  { 3500, 14500 }, { 3000, 14400 }, { 2500, 14300 },
    { 2000, 14150 },  { 1500, 13950 }, { 1000, 13800 }, { 500, 13600 },
    { 0, 13200 },
};

struct io_channel_config {
    uint8_t channel;
};

struct divider_config {
    struct io_channel_config io_channel;
    /* output_ohm is used as a flag value: if it is nonzero then
      * the battery is measured through a voltage divider;
      * otherwise it is assumed to be directly connected to Vdd.
      */
    uint32_t output_ohm;
    uint32_t full_ohm;
};

static const struct divider_config divider_config = {
#if DT_NODE_HAS_STATUS_OKAY(VBATT)
     .io_channel = {
         DT_IO_CHANNELS_INPUT(VBATT),
     },
     .output_ohm = DT_PROP(VBATT, output_ohms),
     .full_ohm = DT_PROP(VBATT, full_ohms),
#endif /* /vbatt exists */
 };

struct divider_data {
    const struct device   *adc;
    struct adc_channel_cfg adc_cfg;
    struct adc_sequence    adc_seq;
    int16_t                raw;
};
static struct divider_data divider_data = {
#if DT_NODE_HAS_STATUS_OKAY(VBATT)
    .adc = DEVICE_DT_GET(DT_IO_CHANNELS_CTLR(VBATT)),
#endif
};

/**
 * @brief Set up the ADC voltage divider measurement channel.
 *
 * @retval 0        on success
 * @retval -ENOENT  if the ADC device is not ready
 */
static int divider_setup(void)
{
    const struct divider_config    *cfg  = &divider_config;
    const struct io_channel_config *iocp = &cfg->io_channel;
    struct divider_data            *ddp  = &divider_data;
    struct adc_sequence            *asp  = &ddp->adc_seq;
    struct adc_channel_cfg         *accp = &ddp->adc_cfg;
    int                             rc;

    if (!device_is_ready(ddp->adc)) {
        LOG_ERR("ADC device is not ready %s", ddp->adc->name);
        return -ENOENT;
    }

    *asp = (struct adc_sequence){
        .channels     = BIT(iocp->channel),
        .buffer       = &ddp->raw,
        .buffer_size  = sizeof(ddp->raw),
        .oversampling = CONFIG_RPR_ADC_OVERSAMPLING,
#ifdef CONFIG_RPR_ADC_CALIBRATE
        .calibrate = true,
#else
        .calibrate = false,
#endif
    };

#ifdef CONFIG_ADC_STM32
    *accp = (struct adc_channel_cfg){
        .gain             = BATTERY_ADC_GAIN,
        .reference        = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME_MAX,
        .channel_id       = iocp->channel,
        .differential     = 0,
    };

    asp->resolution = CONFIG_RPR_ADC_RESOLUTION;

#else /* CONFIG_ADC_var */
#error Unsupported ADC
#endif /* CONFIG_ADC_var */

    rc = adc_channel_setup(ddp->adc, accp);
    LOG_DBG("Setup AIN%u got %d", iocp->channel, rc);

    return rc;
}

static bool battery_ok;

static int battery_setup(void)
{
    int rc = divider_setup();

    battery_ok = (rc == 0);
    LOG_DBG("Battery setup: %d %d", rc, battery_ok);
    return rc;
}

SYS_INIT(battery_setup, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/** @brief Measure the battery voltage.
  *
  * @return the battery voltage in millivolts, or a negative error
  * code.
  */
int battery_sample(void)
{
    int rc = -ENOENT;

    if (battery_ok) {
        struct divider_data         *ddp = &divider_data;
        const struct divider_config *dcp = &divider_config;
        struct adc_sequence         *sp  = &ddp->adc_seq;

        rc            = adc_read(ddp->adc, sp);
        sp->calibrate = false;
        if (rc == 0) {
            int32_t val = ddp->raw;

            adc_raw_to_millivolts(adc_ref_internal(ddp->adc),
                                  ddp->adc_cfg.gain,
                                  sp->resolution,
                                  &val);

            if (dcp->output_ohm != 0) {
                rc = val * (uint64_t)dcp->full_ohm / dcp->output_ohm;
                LOG_DBG("raw %u ~ %u mV => %d mV", ddp->raw, val, rc);
            } else {
                rc = val;
                LOG_DBG("raw %u ~ %u mV", ddp->raw, val);
            }
        }
    }

    return rc;
}

/** @brief Calculate the estimated battery level based on a measured voltage.
  *
  * @param batt_mV a measured battery voltage level.
  *
  * @return the estimated remaining capacity in parts per ten
  * thousand.
  */
unsigned int battery_level_pptt(unsigned int batt_mV)
{
    const struct battery_level_point *pb = levels;

    if (batt_mV >= pb->lvl_mV) {
        /* Measured voltage above highest point, cap at maximum. */
        return pb->lvl_pptt;
    }
    /* Go down to the last point at or below the measured voltage. */
    while ((pb->lvl_pptt > 0) && (batt_mV < pb->lvl_mV)) {
        ++pb;
    }
    if (batt_mV < pb->lvl_mV) {
        /* Below lowest point, cap at minimum */
        return pb->lvl_pptt;
    }

    /* Linear interpolation between below and above points. */
    const struct battery_level_point *pa = pb - 1;

    return pb->lvl_pptt + ((pa->lvl_pptt - pb->lvl_pptt) *
                           (batt_mV - pb->lvl_mV) / (pa->lvl_mV - pb->lvl_mV));
}