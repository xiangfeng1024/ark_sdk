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

#include "ark_hal_usb_device.h"

#include "FreeRTOS.h"
#include "task.h"
#include "usbd_cdc.h"
#include "ark_hal_bindings.h"

#define ARK_USB_CDC_RX_PACKET_SIZE CDC_DATA_FS_OUT_PACKET_SIZE
#define ARK_USB_CDC_LINE_CODING_SIZE 7U
#define ARK_USB_CDC_DEFAULT_BITRATE 115200U
#define ARK_USB_CDC_DEFAULT_DATA_BITS 8U
#define ARK_USB_CDC_TX_WAIT_TICKS 1U

static uint8_t ark_usb_cdc_rx_packet[ARK_USB_CDC_RX_PACKET_SIZE];
static ark_hal_usb_receive_callback_t ark_usb_receive_callback;
static void *ark_usb_receive_context;
static USBD_CDC_LineCodingTypeDef ark_usb_line_coding = {
    .bitrate = ARK_USB_CDC_DEFAULT_BITRATE,
    .format = 0U,
    .paritytype = 0U,
    .datatype = ARK_USB_CDC_DEFAULT_DATA_BITS,
};

static USBD_HandleTypeDef *stm32f1_usb_device_handle(void) {
    return (USBD_HandleTypeDef *)ark_hal_usb_device_handle;
}

static int8_t stm32f1_usb_cdc_init(void) {
    USBD_HandleTypeDef *device = stm32f1_usb_device_handle();

    if ((device == NULL) ||
        (USBD_CDC_SetRxBuffer(device, ark_usb_cdc_rx_packet) != USBD_OK)) {
        return (int8_t)USBD_FAIL;
    }
    if ((device->dev_state == USBD_STATE_CONFIGURED) &&
        (USBD_CDC_ReceivePacket(device) != USBD_OK)) {
        return (int8_t)USBD_FAIL;
    }
    return (int8_t)USBD_OK;
}

static int8_t stm32f1_usb_cdc_deinit(void) {
    return (int8_t)USBD_OK;
}

static int8_t stm32f1_usb_cdc_control(
    uint8_t command,
    uint8_t *buffer,
    uint16_t length) {
    if ((buffer == NULL) && (length != 0U)) {
        return (int8_t)USBD_FAIL;
    }

    if ((command == CDC_SET_LINE_CODING) &&
        (length >= ARK_USB_CDC_LINE_CODING_SIZE)) {
        ark_usb_line_coding.bitrate =
            (uint32_t)buffer[0] |
            ((uint32_t)buffer[1] << 8U) |
            ((uint32_t)buffer[2] << 16U) |
            ((uint32_t)buffer[3] << 24U);
        ark_usb_line_coding.format = buffer[4];
        ark_usb_line_coding.paritytype = buffer[5];
        ark_usb_line_coding.datatype = buffer[6];
    } else if ((command == CDC_GET_LINE_CODING) &&
               (length >= ARK_USB_CDC_LINE_CODING_SIZE)) {
        buffer[0] = (uint8_t)ark_usb_line_coding.bitrate;
        buffer[1] = (uint8_t)(ark_usb_line_coding.bitrate >> 8U);
        buffer[2] = (uint8_t)(ark_usb_line_coding.bitrate >> 16U);
        buffer[3] = (uint8_t)(ark_usb_line_coding.bitrate >> 24U);
        buffer[4] = ark_usb_line_coding.format;
        buffer[5] = ark_usb_line_coding.paritytype;
        buffer[6] = ark_usb_line_coding.datatype;
    }

    return (int8_t)USBD_OK;
}

static int8_t stm32f1_usb_cdc_receive(uint8_t *buffer, uint32_t *length) {
    USBD_HandleTypeDef *device = stm32f1_usb_device_handle();

    if ((buffer != NULL) && (length != NULL) && (*length > 0U) &&
        (ark_usb_receive_callback != NULL)) {
        ark_usb_receive_callback(buffer, (size_t)*length, ark_usb_receive_context);
    }

    if (device != NULL) {
        USBD_CDC_SetRxBuffer(device, ark_usb_cdc_rx_packet);
        USBD_CDC_ReceivePacket(device);
    }
    return (int8_t)USBD_OK;
}

static USBD_CDC_ItfTypeDef ark_usb_cdc_interface = {
    .Init = stm32f1_usb_cdc_init,
    .DeInit = stm32f1_usb_cdc_deinit,
    .Control = stm32f1_usb_cdc_control,
    .Receive = stm32f1_usb_cdc_receive,
};

static bool stm32f1_usb_device_initialize(
    ark_hal_usb_receive_callback_t receive_callback,
    void *receive_context) {
    USBD_HandleTypeDef *device = stm32f1_usb_device_handle();

    if ((device == NULL) || (receive_callback == NULL)) {
        return false;
    }

    ark_usb_receive_callback = receive_callback;
    ark_usb_receive_context = receive_context;
    if (USBD_CDC_RegisterInterface(device, &ark_usb_cdc_interface) != USBD_OK) {
        return false;
    }
    if (device->pClassData != NULL) {
        return stm32f1_usb_cdc_init() == (int8_t)USBD_OK;
    }
    return true;
}

static bool stm32f1_usb_device_is_ready(void) {
    USBD_HandleTypeDef *device = stm32f1_usb_device_handle();

    return (device != NULL) &&
           (device->pUserData == &ark_usb_cdc_interface);
}

static bool stm32f1_usb_device_is_configured(void) {
    USBD_HandleTypeDef *device = stm32f1_usb_device_handle();

    return stm32f1_usb_device_is_ready() && (device->pClassData != NULL) &&
           (device->dev_state == USBD_STATE_CONFIGURED);
}

static int32_t stm32f1_usb_device_transmit(
    const uint8_t *data,
    size_t size,
    uint32_t timeout_ms) {
    USBD_HandleTypeDef *device = stm32f1_usb_device_handle();
    USBD_CDC_HandleTypeDef *cdc;
    TickType_t start_tick;
    TickType_t timeout_ticks;

    if (!stm32f1_usb_device_is_configured() || (data == NULL) ||
        (size == 0U) || (size > UINT16_MAX)) {
        return -1;
    }

    cdc = (USBD_CDC_HandleTypeDef *)device->pClassData;
    start_tick = xTaskGetTickCount();
    timeout_ticks = (timeout_ms == UINT32_MAX) ?
        portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    while (cdc->TxState != 0U) {
        if ((timeout_ticks != portMAX_DELAY) &&
            ((xTaskGetTickCount() - start_tick) >= timeout_ticks)) {
            return -1;
        }
        vTaskDelay(ARK_USB_CDC_TX_WAIT_TICKS);
    }

    if ((USBD_CDC_SetTxBuffer(device, (uint8_t *)data, (uint16_t)size) != USBD_OK) ||
        (USBD_CDC_TransmitPacket(device) != USBD_OK)) {
        return -1;
    }

    while (cdc->TxState != 0U) {
        if ((timeout_ticks != portMAX_DELAY) &&
            ((xTaskGetTickCount() - start_tick) >= timeout_ticks)) {
            return -1;
        }
        vTaskDelay(ARK_USB_CDC_TX_WAIT_TICKS);
    }
    return (int32_t)size;
}

const ark_hal_usb_device_driver_t ark_hal_usb_device = {
    .initialize = stm32f1_usb_device_initialize,
    .is_ready = stm32f1_usb_device_is_ready,
    .is_configured = stm32f1_usb_device_is_configured,
    .transmit = stm32f1_usb_device_transmit,
};
