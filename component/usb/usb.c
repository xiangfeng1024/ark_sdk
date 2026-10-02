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

#include "usb.h"

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "stream_buffer.h"
#include "ark_component.h"
#include "ark_dts.h"
#include "ark_hal_usb_device.h"
#include "ark_stream.h"

#if defined(ARK_USB_DEVICE_CUBEMX_INIT)
#include "usb_device.h"
#endif

#define USB_STREAM_RX_BUFFER_SIZE 256U
#define USB_SELF_TEST_EXPECTED_MS 20U

typedef struct {
    uint8_t rx_storage[USB_STREAM_RX_BUFFER_SIZE];
    StaticStreamBuffer_t rx_control;
    StreamBufferHandle_t rx;
    StaticSemaphore_t tx_mutex_control;
    SemaphoreHandle_t tx_mutex;
    bool ready;
} usb_context_t;

static usb_context_t usb_context;
static ark_stream_t usb_stream;
static usb_config_t usb_config;

const usb_config_t *usb_config_get(void) {
    return &usb_config;
}

static bool usb_load_config(void) {
    const ark_of_node_t *node =
        ark_of_find_compatible_node(NULL, "usb");

    return (node != NULL) &&
           (ark_of_property_read_string(
                node, "ark,stream-name", &usb_config.stream_name) == 0);
}

static TickType_t usb_timeout_ticks(uint32_t timeout_ms) {
    return (timeout_ms == ARK_STREAM_WAIT_FOREVER) ?
           portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
}

static void usb_receive_callback(
    const uint8_t *data,
    size_t size,
    void *context) {
    usb_context_t *usb = context;
    BaseType_t higher_priority_task_woken = pdFALSE;

    if ((usb != NULL) && (usb->rx != NULL) && (data != NULL) && (size > 0U)) {
        xStreamBufferSendFromISR(
            usb->rx,
            data,
            size,
            &higher_priority_task_woken);
    }
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static bool usb_stream_is_ready(void *context) {
    return ((usb_context_t *)context)->ready;
}

static int32_t usb_stream_read(
    void *context,
    uint8_t *data,
    size_t size,
    uint32_t timeout_ms) {
    usb_context_t *usb = context;

    if (!usb->ready) {
        return -1;
    }
    return (int32_t)xStreamBufferReceive(
        usb->rx,
        data,
        size,
        usb_timeout_ticks(timeout_ms));
}

static int32_t usb_stream_write(
    void *context,
    const uint8_t *data,
    size_t size,
    uint32_t timeout_ms) {
    usb_context_t *usb = context;
    TickType_t timeout = usb_timeout_ticks(timeout_ms);
    int32_t result;

    if (!usb->ready ||
        (xSemaphoreTake(usb->tx_mutex, timeout) != pdTRUE)) {
        return -1;
    }
    result = ark_hal_usb_device.transmit(data, size, timeout_ms);
    (void)xSemaphoreGive(usb->tx_mutex);
    return result;
}

static const ark_stream_operations_t usb_stream_operations = {
    .is_ready = usb_stream_is_ready,
    .read = usb_stream_read,
    .write = usb_stream_write,
};

static ark_component_result_t usb_init(void) {
#if defined(ARK_USB_DEVICE_CUBEMX_INIT)
    MX_USB_DEVICE_Init();
#endif

    if ((ark_usb_config.stream_name == NULL) ||
        (ark_usb_config.stream_name[0] == '\0')) {
        return ARK_COMPONENT_ERROR;
    }

    usb_context.rx = xStreamBufferCreateStatic(
        sizeof(usb_context.rx_storage),
        1U,
        usb_context.rx_storage,
        &usb_context.rx_control);
    usb_context.tx_mutex =
        xSemaphoreCreateMutexStatic(&usb_context.tx_mutex_control);
    if ((usb_context.rx == NULL) || (usb_context.tx_mutex == NULL)) {
        return ARK_COMPONENT_ERROR;
    }

    usb_stream.name = ark_usb_config.stream_name;
    usb_stream.operations = &usb_stream_operations;
    usb_stream.context = &usb_context;
    if (!ark_stream_attach(&usb_stream)) {
        return ARK_COMPONENT_ERROR;
    }
    if (!ark_hal_usb_device.initialize(usb_receive_callback, &usb_context)) {
        ark_stream_detach(&usb_stream);
        return ARK_COMPONENT_ERROR;
    }

    usb_context.ready = true;
    return ARK_COMPONENT_OK;
}

static ark_component_result_t usb_self_test(void) {
    return ark_hal_usb_device.is_ready() ?
           ARK_COMPONENT_OK : ARK_COMPONENT_ERROR;
}

static const ark_component_t usb_component = {
    .name = "usb",
    .init = usb_init,
    .self_test = usb_self_test,
    .self_test_expected_ms = USB_SELF_TEST_EXPECTED_MS,
    .init_level = ARK_COMPONENT_INIT_LEVEL_2,
};

bool usb_register(void) {
    return usb_load_config() && ark_component_register(&usb_component);
}
