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

#ifndef ARK_HAL_GPIO_H
#define ARK_HAL_GPIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ARK_HAL_GPIO_PORT_A = 0,
    ARK_HAL_GPIO_PORT_B,
    ARK_HAL_GPIO_PORT_C,
    ARK_HAL_GPIO_PORT_D,
    ARK_HAL_GPIO_PORT_E,
    ARK_HAL_GPIO_PORT_F,
    ARK_HAL_GPIO_PORT_G
} ark_hal_gpio_port_t;

typedef uint16_t ark_hal_gpio_pin_t;
typedef void (*ark_hal_gpio_irq_callback_t)(void *context);

typedef enum {
    ARK_HAL_GPIO_MODE_INPUT = 0,
    ARK_HAL_GPIO_MODE_OUTPUT_PUSH_PULL,
    ARK_HAL_GPIO_MODE_OUTPUT_OPEN_DRAIN
} ark_hal_gpio_mode_t;

#define ARK_HAL_GPIO_PIN(number) ((ark_hal_gpio_pin_t)(1UL << (number)))

typedef struct {
    ark_hal_gpio_port_t port;
    ark_hal_gpio_pin_t pin;
    uint8_t active_level;
} ark_hal_gpio_config_t;

typedef struct {
    void (*set)(ark_hal_gpio_port_t port, ark_hal_gpio_pin_t pin, bool high);
    bool (*get)(ark_hal_gpio_port_t port, ark_hal_gpio_pin_t pin);
    void (*toggle)(ark_hal_gpio_port_t port, ark_hal_gpio_pin_t pin);
    bool (*transmit_pulse_bytes)(
        ark_hal_gpio_port_t port,
        ark_hal_gpio_pin_t pin,
        const uint8_t *data,
        size_t size,
        uint32_t period_ns,
        uint32_t zero_high_ns,
        uint32_t one_high_ns);
    bool (*configure)(
        ark_hal_gpio_port_t port,
        ark_hal_gpio_pin_t pin,
        ark_hal_gpio_mode_t mode,
        bool pull_up);
    bool (*register_irq)(
        ark_hal_gpio_port_t port,
        ark_hal_gpio_pin_t pin,
        ark_hal_gpio_irq_callback_t callback,
        void *context);
} ark_hal_gpio_driver_t;

extern const ark_hal_gpio_driver_t ark_hal_gpio;

#endif /* ARK_HAL_GPIO_H */
