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

#include "adc_sensor.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ark_cli.h"
#include "ark_component.h"
#include "ark_dts.h"
#include "ark_hal_adc.h"

#define ADC_SENSOR_REFERENCE_MV 3300U
#define ADC_SENSOR_FULL_SCALE 4095U
#define ADC_SENSOR_MAX_COUNT 3U

typedef struct {
    adc_sensor_kind_t kind;
    const char *name;
    ark_hal_adc_id_t adc_id;
    uint32_t channel;
} adc_sensor_instance_config_t;

typedef struct {
    const adc_sensor_instance_config_t *instances;
    size_t count;
} adc_sensor_config_t;

static adc_sensor_instance_config_t adc_sensor_instances[ADC_SENSOR_MAX_COUNT];
static adc_sensor_config_t adc_sensor_config;

static bool adc_sensor_kind_from_name(
    const char *name,
    adc_sensor_kind_t *kind) {
    if ((name == NULL) || (kind == NULL)) {
        return false;
    }
    if (strcmp(name, "mq7") == 0) {
        *kind = ADC_SENSOR_MQ7;
    } else if (strcmp(name, "mq135") == 0) {
        *kind = ADC_SENSOR_MQ135;
    } else if (strcmp(name, "soil") == 0) {
        *kind = ADC_SENSOR_SOIL;
    } else {
        return false;
    }
    return true;
}

static bool adc_sensor_load_one(
    const ark_of_node_t *node,
    size_t index) {
    const char *name;
    adc_sensor_kind_t kind;
    uint8_t adc_id;
    uint32_t channel;

    if ((node == NULL) ||
        (ark_of_property_read_string(node, "label", &name) != 0) ||
        !adc_sensor_kind_from_name(name, &kind) ||
        (ark_of_hal_get_parent(node, ARK_OF_HAL_ADC, &adc_id) != 0) ||
        (ark_of_property_read_u32(node, "reg", &channel) != 0)) {
        return false;
    }
    adc_sensor_instances[index].kind = kind;
    adc_sensor_instances[index].name = name;
    adc_sensor_instances[index].adc_id = (ark_hal_adc_id_t)adc_id;
    adc_sensor_instances[index].channel = channel;
    return true;
}

static bool adc_sensor_load_config(void) {
    const ark_of_node_t *node = NULL;
    size_t count = 0U;

    while ((node = ark_of_find_compatible_node(node, "adc_sensor")) != NULL) {
        size_t previous;

        if ((count >= ADC_SENSOR_MAX_COUNT) ||
            !adc_sensor_load_one(node, count)) {
            return false;
        }
        for (previous = 0U; previous < count; ++previous) {
            if (adc_sensor_instances[previous].kind ==
                adc_sensor_instances[count].kind) {
                return false;
            }
        }
        count++;
    }
    adc_sensor_config.instances = adc_sensor_instances;
    adc_sensor_config.count = count;
    return count != 0U;
}

static const adc_sensor_instance_config_t *adc_sensor_find(adc_sensor_kind_t kind) {
    size_t index;

    for (index = 0U; index < adc_sensor_config.count; ++index) {
        if (adc_sensor_config.instances[index].kind == kind) {
            return &adc_sensor_config.instances[index];
        }
    }
    return NULL;
}

bool adc_sensor_read(adc_sensor_kind_t kind, adc_sensor_sample_t *sample) {
    const adc_sensor_instance_config_t *instance = adc_sensor_find(kind);
    uint16_t raw;

    if ((instance == NULL) || (sample == NULL) ||
        !ark_hal_adc.read(instance->adc_id, instance->channel, &raw, 20U)) {
        return false;
    }
    sample->raw = raw;
    sample->millivolts = (uint16_t)(((uint32_t)raw * ADC_SENSOR_REFERENCE_MV) /
                                    ADC_SENSOR_FULL_SCALE);
    sample->tick = xTaskGetTickCount();
    return true;
}

static ark_component_result_t adc_sensor_init(void) {
    size_t index;

    if (!adc_sensor_load_config() || (adc_sensor_config.instances == NULL) ||
        (adc_sensor_config.count == 0U) ||
        (adc_sensor_config.count > ADC_SENSOR_MAX_COUNT)) {
        return ARK_COMPONENT_ERROR;
    }
    for (index = 0U; index < adc_sensor_config.count; ++index) {
        const adc_sensor_instance_config_t *instance = &adc_sensor_config.instances[index];

        if (!ark_hal_adc.is_ready(instance->adc_id)) {
            return ARK_COMPONENT_ERROR;
        }
    }
    return ARK_COMPONENT_OK;
}

static ark_component_result_t adc_sensor_self_test(void) {
    size_t index;

    for (index = 0U; index < adc_sensor_config.count; ++index) {
        adc_sensor_sample_t sample;

        if (!adc_sensor_read(adc_sensor_config.instances[index].kind, &sample)) {
            return ARK_COMPONENT_ERROR;
        }
    }
    return ARK_COMPONENT_OK;
}

#if ARK_DTS_HAS_UART_CLI
static int adc_sensor_cli(int argc, char *argv[]) {
    size_t index;

    if (argc != 2) {
        printf("usage: %s <mq7|mq135|soil>\r\n", argv[0]);
        return -1;
    }
    for (index = 0U; index < adc_sensor_config.count; ++index) {
        const adc_sensor_instance_config_t *instance = &adc_sensor_config.instances[index];

        if (strcmp(argv[1], instance->name) == 0) {
            adc_sensor_sample_t sample;

            if (!adc_sensor_read(instance->kind, &sample)) {
                printf("%s read failed\r\n", instance->name);
                return -1;
            }
            printf("%s raw=%u voltage=%u mV\r\n",
                   instance->name, sample.raw, sample.millivolts);
            return 0;
        }
    }
    printf("unknown adc sensor: %s\r\n", argv[1]);
    return -1;
}

static ark_cli_command_t adc_sensor_command = {
    .name = "adc_read",
    .usage = "adc_read <mq7|mq135|soil>",
    .description = "read raw ADC sensor value",
    .handler = adc_sensor_cli,
};
#endif

static const ark_component_t adc_sensor_component = {
    .name = "adc_sensor",
    .init = adc_sensor_init,
    .self_test = adc_sensor_self_test,
    .self_test_expected_ms = 100U,
    .init_level = ARK_COMPONENT_INIT_LEVEL_3,
};

bool adc_sensor_register(void) {
#if ARK_DTS_HAS_UART_CLI
    (void)ark_cli_register(&adc_sensor_command);
#endif
    return ark_component_register(&adc_sensor_component);
}
