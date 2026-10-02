/**
 * SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0
 *
 * ARK CREW LIMITED NON-COMMERCIAL LICENSE NOTICE
 *
 * This source code, together with its associated documentation, examples,
 * configuration files, and related materials, is collectively referred to
 * as the "Software".
 *
 * Subject to the complete terms set forth in the LICENSE file, Ark Crew
 * grants you a limited, non-exclusive, non-transferable, and non-sublicensable
 * right to access, reproduce, and modify the Software solely for personal
 * study, classroom education, academic research, and non-commercial evaluation.
 *
 * Commercial use of the Software, in whole or in part, is strictly prohibited
 * without prior written authorization from Ark Crew. Prohibited activities
 * include, without limitation, sale, sublicensing, paid distribution, use in
 * paid consulting or training, incorporation into any commercial product or
 * service, and internal development intended for commercial deployment.
 *
 * Except for the limited rights expressly granted under the applicable
 * License, no license or other right, whether express, implied, by estoppel,
 * or otherwise, is granted under any copyright, patent, trademark, trade
 * secret, mask work, or other intellectual property right belonging to
 * Ark Crew or any third party.
 *
 * Delivery or disclosure of the Software does not convey permission to use
 * the Ark Crew name, trademarks, logos, visual identity, or other branding,
 * except where strictly necessary to preserve the original attribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND "WITH ALL FAULTS", WITHOUT ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND, WHETHER EXPRESS, IMPLIED,
 * STATUTORY, OR OTHERWISE, INCLUDING WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, TITLE, ACCURACY, RELIABILITY, AND
 * NON-INFRINGEMENT.
 *
 * TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, ARK CREW SHALL NOT
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL LOSS OR DAMAGE ARISING FROM OR RELATED TO
 * THE SOFTWARE, ITS USE, OR ITS INABILITY TO BE USED.
 *
 * This notice shall be retained in all authorized copies or substantial
 * portions of the Software. Removal, concealment, or unauthorized alteration
 * of this notice is prohibited.
 *
 * See the LICENSE file in the root directory of this repository for the
 * complete and controlling license terms.
 */

#include "adc38_tracking.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ark_cli.h"
#include "ark_component.h"
#include "ark_dts.h"
#include "ark_hal_adc.h"
#include "ark_hal_gpio.h"
#include "ark_hal_time.h"

typedef struct {
    ark_hal_adc_id_t adc_id;
    uint32_t adc_channel;
    ark_hal_gpio_config_t address[3];
    uint16_t white[ADC38_TRACKING_CHANNEL_COUNT];
    uint16_t black[ADC38_TRACKING_CHANNEL_COUNT];
    uint8_t samples_per_channel;
    uint16_t settle_us;
} adc38_tracking_config_t;

static const int16_t adc38_position_weights[ADC38_TRACKING_CHANNEL_COUNT] = {
    -2048, -1463, -878, -293, 293, 878, 1463, 2048
};
static adc38_tracking_calibration_t adc38_calibration;
static bool adc38_initialized;
static adc38_tracking_config_t adc38_config;

static bool adc38_tracking_load_config(void) {
    const ark_of_node_t *node =
        ark_of_find_compatible_node(NULL, "adc38_tracking");
    const ark_of_node_t *calibration =
        ark_of_find_node_by_path("/software/calibration");
    uint32_t white[ADC38_TRACKING_CHANNEL_COUNT];
    uint32_t black[ADC38_TRACKING_CHANNEL_COUNT];
    uint32_t value;
    uint8_t adc_id;
    uint8_t index;

    if ((node == NULL) || (calibration == NULL) ||
        (ark_of_hal_get(node, "io-channels", 0U, ARK_OF_HAL_ADC,
                       &adc_id, &adc38_config.adc_channel) != 0) ||
        (ark_of_property_read_u32_array(
             calibration, "ark,white", white, ADC38_TRACKING_CHANNEL_COUNT) != 0) ||
        (ark_of_property_read_u32_array(
             calibration, "ark,black", black, ADC38_TRACKING_CHANNEL_COUNT) != 0) ||
        (ark_of_property_read_u32(calibration, "ark,samples-per-channel", &value) != 0) ||
        (value > UINT8_MAX)) {
        return false;
    }
    adc38_config.adc_id = (ark_hal_adc_id_t)adc_id;
    adc38_config.samples_per_channel = (uint8_t)value;
    if ((ark_of_property_read_u32(calibration, "ark,settle-us", &value) != 0) ||
        (value > UINT16_MAX)) {
        return false;
    }
    adc38_config.settle_us = (uint16_t)value;
    for (index = 0U; index < 3U; ++index) {
        if (ark_of_get_named_gpio(node, "select-gpios", index,
                                 &adc38_config.address[index]) != 0) {
            return false;
        }
    }
    for (index = 0U; index < ADC38_TRACKING_CHANNEL_COUNT; ++index) {
        if ((white[index] > UINT16_MAX) || (black[index] > UINT16_MAX)) {
            return false;
        }
        adc38_config.white[index] = (uint16_t)white[index];
        adc38_config.black[index] = (uint16_t)black[index];
    }
    return true;
}

static bool adc38_calibration_valid(
    const adc38_tracking_calibration_t *calibration) {
    uint8_t index;

    if ((adc38_config.samples_per_channel == 0U) ||
        (adc38_config.samples_per_channel > 16U)) {
        return false;
    }
    for (index = 0U; index < ADC38_TRACKING_CHANNEL_COUNT; ++index) {
        if ((calibration->white[index] > 4095U) ||
            (calibration->black[index] > 4095U) ||
            (calibration->white[index] == calibration->black[index])) {
            return false;
        }
    }
    return true;
}

static void adc38_select(uint8_t channel) {
    uint8_t index;

    for (index = 0U; index < 3U; ++index) {
        bool is_high = ((channel >> index) & 1U) != 0U;
        const ark_hal_gpio_config_t *address =
            &adc38_config.address[index];

        ark_hal_gpio.set(address->port, address->pin,
                        is_high ? address->active_level != 0U :
                                  address->active_level == 0U);
    }
    ark_hal_time.delay_us(adc38_config.settle_us);
}

bool adc38_tracking_scan_raw(uint16_t raw[ADC38_TRACKING_CHANNEL_COUNT]) {
    uint8_t channel;

    if ((raw == NULL) || !ark_hal_adc.is_ready(adc38_config.adc_id)) {
        return false;
    }
    for (channel = 0U; channel < ADC38_TRACKING_CHANNEL_COUNT; ++channel) {
        uint32_t sum = 0U;
        uint8_t sample;

        adc38_select(channel);
        for (sample = 0U; sample < adc38_config.samples_per_channel; ++sample) {
            uint16_t value;

            if (!ark_hal_adc.read(
                    adc38_config.adc_id,
                    adc38_config.adc_channel,
                    &value,
                    20U)) {
                return false;
            }
            sum += value;
        }
        raw[channel] = (uint16_t)(sum / adc38_config.samples_per_channel);
    }
    return true;
}

static bool adc38_process(
    const uint16_t raw[ADC38_TRACKING_CHANNEL_COUNT],
    adc38_tracking_sample_t *sample) {
    adc38_tracking_calibration_t calibration;
    int64_t weighted = 0;
    uint32_t strength_sum = 0U;
    uint8_t channel;

    taskENTER_CRITICAL();
    calibration = adc38_calibration;
    taskEXIT_CRITICAL();
    memset(sample, 0, sizeof(*sample));
    for (channel = 0U; channel < ADC38_TRACKING_CHANNEL_COUNT; ++channel) {
        uint16_t white = calibration.white[channel];
        uint16_t black = calibration.black[channel];
        uint32_t span = (white > black) ?
                        (uint32_t)white - black : (uint32_t)black - white;
        uint32_t normalized;
        uint32_t strength;

        sample->raw[channel] = raw[channel];
        if (span == 0U) {
            continue;
        }
        if (white > black) {
            if (raw[channel] <= black) {
                normalized = 0U;
            } else if (raw[channel] >= white) {
                normalized = 4095U;
            } else {
                normalized = ((uint32_t)(raw[channel] - black) * 4095U) / span;
            }
        } else {
            if (raw[channel] <= white) {
                normalized = 4095U;
            } else if (raw[channel] >= black) {
                normalized = 0U;
            } else {
                normalized = ((uint32_t)(black - raw[channel]) * 4095U) / span;
            }
        }
        strength = 4095U - normalized;
        sample->normalized[channel] = (uint16_t)normalized;
        if (strength > 512U) {
            sample->line_mask |= (uint8_t)(1U << channel);
        }
        strength_sum += strength;
        weighted += (int64_t)strength * adc38_position_weights[channel];
        sample->valid_mask |= (uint8_t)(1U << channel);
    }
    if (strength_sum != 0U) {
        sample->position = (int16_t)(weighted / (int32_t)strength_sum);
    }
    sample->calibrated = adc38_calibration_valid(&calibration);
    sample->adc_ok = true;
    sample->tick = xTaskGetTickCount();
    return sample->calibrated;
}

bool adc38_tracking_read(adc38_tracking_sample_t *sample) {
    uint16_t raw[ADC38_TRACKING_CHANNEL_COUNT];

    if ((sample == NULL) || !adc38_tracking_scan_raw(raw)) {
        return false;
    }
    return adc38_process(raw, sample);
}

bool adc38_tracking_calibrated(void) {
    adc38_tracking_calibration_t calibration;

    return adc38_tracking_get_calibration(&calibration) &&
           adc38_calibration_valid(&calibration);
}

bool adc38_tracking_get_calibration(adc38_tracking_calibration_t *calibration) {
    if ((calibration == NULL) || !adc38_initialized) {
        return false;
    }
    taskENTER_CRITICAL();
    *calibration = adc38_calibration;
    taskEXIT_CRITICAL();
    return true;
}

bool adc38_tracking_set_calibration(
    const adc38_tracking_calibration_t *calibration) {
    if ((calibration == NULL) ||
        !adc38_calibration_valid(calibration)) {
        return false;
    }
    taskENTER_CRITICAL();
    adc38_calibration = *calibration;
    taskEXIT_CRITICAL();
    return true;
}

bool adc38_tracking_capture_white(uint16_t values[ADC38_TRACKING_CHANNEL_COUNT]) {
    adc38_tracking_calibration_t calibration;

    if (!adc38_tracking_get_calibration(&calibration) ||
        !adc38_tracking_scan_raw(calibration.white) ||
        !adc38_tracking_set_calibration(&calibration)) {
        return false;
    }
    if (values != NULL) {
        memcpy(values, calibration.white, sizeof(calibration.white));
    }
    return true;
}

bool adc38_tracking_capture_black(uint16_t values[ADC38_TRACKING_CHANNEL_COUNT]) {
    adc38_tracking_calibration_t calibration;

    if (!adc38_tracking_get_calibration(&calibration) ||
        !adc38_tracking_scan_raw(calibration.black) ||
        !adc38_tracking_set_calibration(&calibration)) {
        return false;
    }
    if (values != NULL) {
        memcpy(values, calibration.black, sizeof(calibration.black));
    }
    return true;
}

#if ARK_DTS_HAS_UART_CLI
static int adc38_tracking_cli(int argc, char *argv[]) {
    uint16_t raw[ADC38_TRACKING_CHANNEL_COUNT];
    uint8_t index;

    if ((argc != 2) ||
        ((strcmp(argv[1], "white") != 0) && (strcmp(argv[1], "black") != 0))) {
        printf("usage: track_cal <white|black>\r\n");
        return -1;
    }
    if (!adc38_tracking_scan_raw(raw)) {
        printf("track_cal read failed\r\n");
        return -1;
    }
    printf("track_cal %s:", argv[1]);
    for (index = 0U; index < ADC38_TRACKING_CHANNEL_COUNT; ++index) {
        printf(" %u", (unsigned int)raw[index]);
    }
    printf("\r\n");
    return 0;
}

static int adc38_tracking_probe_cli(int argc, char *argv[]) {
    uint8_t channel;

    if (argc != 1) {
        printf("usage: track_probe\r\n");
        return -1;
    }
    for (channel = 0U; channel < ADC38_TRACKING_CHANNEL_COUNT; ++channel) {
        uint16_t raw;
        const ark_hal_gpio_config_t *a0 = &adc38_config.address[0];
        const ark_hal_gpio_config_t *a1 = &adc38_config.address[1];
        const ark_hal_gpio_config_t *a2 = &adc38_config.address[2];

        adc38_select(channel);
        if (!ark_hal_adc.read(
                adc38_config.adc_id,
                adc38_config.adc_channel,
                &raw,
                20U)) {
            printf("track_probe ch=%u adc=FAIL\r\n", (unsigned int)channel);
            return -1;
        }
        printf("track_probe ch=%u req=%u%u%u gpio=%u%u%u raw=%u\r\n",
               (unsigned int)channel,
               (unsigned int)((channel >> 2U) & 1U),
               (unsigned int)((channel >> 1U) & 1U),
               (unsigned int)(channel & 1U),
               ark_hal_gpio.get(a2->port, a2->pin) ? 1U : 0U,
               ark_hal_gpio.get(a1->port, a1->pin) ? 1U : 0U,
               ark_hal_gpio.get(a0->port, a0->pin) ? 1U : 0U,
               (unsigned int)raw);
    }
    return 0;
}

static ark_cli_command_t adc38_tracking_command = {
    .name = "track_cal",
    .usage = "track_cal <white|black>",
    .description = "scan ADC38 tracking calibration values",
    .handler = adc38_tracking_cli,
};

static ark_cli_command_t adc38_tracking_probe_command = {
    .name = "track_probe",
    .usage = "track_probe",
    .description = "probe decoder GPIO readback and ADC values",
    .handler = adc38_tracking_probe_cli,
};
#endif

static ark_component_result_t adc38_tracking_init(void) {
    uint8_t index;

    if (!adc38_tracking_load_config()) {
        return ARK_COMPONENT_ERROR;
    }
    memcpy(adc38_calibration.white, adc38_config.white,
           sizeof(adc38_calibration.white));
    memcpy(adc38_calibration.black, adc38_config.black,
           sizeof(adc38_calibration.black));
    adc38_initialized = true;
    if (!ark_hal_time.is_ready() || !ark_hal_adc.is_ready(adc38_config.adc_id) ||
        !adc38_calibration_valid(&adc38_calibration)) {
        return ARK_COMPONENT_ERROR;
    }
    for (index = 0U; index < 3U; ++index) {
        const ark_hal_gpio_config_t *address = &adc38_config.address[index];

        if (!ark_hal_gpio.configure(
                address->port, address->pin,
                ARK_HAL_GPIO_MODE_OUTPUT_PUSH_PULL, false)) {
            return ARK_COMPONENT_ERROR;
        }
    }
    adc38_select(0U);
    return ARK_COMPONENT_OK;
}

static ark_component_t adc38_tracking_component = {
    .name = "adc38_tracking",
    .init = adc38_tracking_init,
    .self_test = NULL,
    .self_test_expected_ms = 0U,
    .init_level = ARK_COMPONENT_INIT_LEVEL_3,
};

bool adc38_tracking_register(void) {
#if ARK_DTS_HAS_UART_CLI
    (void)ark_cli_register(&adc38_tracking_command);
    (void)ark_cli_register(&adc38_tracking_probe_command);
#endif
    return ark_component_register(&adc38_tracking_component);
}
