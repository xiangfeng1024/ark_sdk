---
name: ark-sdk-hal
description: 在 ARK CREW SDK 中设计、配置、调试或审查 HAL 驱动、平台适配、CubeMX 句柄绑定、DMA/IRQ 与 UART 管理时使用。
---

# ARK CREW SDK HAL Skill

## 何时读取本 Skill

用户请求涉及 `hal/`、`ark_hal_*`、`ark_uart_manage`、外设句柄、DMA/NVIC/IRQ、CubeMX 生成代码、硬件超时或平台移植时，先读本文件，再读：

- `doc/current_architecture.md`：当前架构事实。
- `doc/ark_sdk_guide.md`：板级验证、协议和已知限制。
- `hal/common/hal_catalog.json`：驱动、平台和源码闭包的唯一清单。
- `hal/include/*.h` 与被修改的 `hal/<platform>/src/*.c`：真实 API；文档不得覆盖代码事实。
- 相关 App 的 `app/<name>/<name>.dts`、`src/ark_dts_generated.c`、CubeMX `.ioc` 和 Keil `.uvprojx`。

## 架构与所有权

调用方向必须保持：

```text
App / component
  -> ark_hal_* 全局驱动对象、ark_uart_manage
    -> 选定平台适配器（当前为 hal/stm32f1）
      -> 生成的 CubeMX handles 与 vendor HAL
```

- 每个公开驱动在 `hal/include/ark_hal_<name>.h` 声明一个 `ark_hal_<name>_driver_t`，并导出同名全局对象（例如 `ark_hal_gpio`）；不得新增运行时平台注册表或 getter。
- `hal/common/hal_catalog.json` 是唯一构建元数据。每个 `hal/<platform>/src/*.c` 及 `hal/src/*.c` 必须且只能归属一个 driver；新增源文件时同步 catalog 的 `sources`、宏和平台信息。
- 当前平台为 `stm32f1`，根 compatible 为 `stm32f103c8` 或 `gd32f103c8`，工程前缀均为 `c8t6`。
- CubeMX 生成的 `Core`、`Drivers`、`Middlewares`、MSP 初始化、DMA 配置、NVIC 和 IRQ 文件只由 CubeMX 管理。不得手改以“修复”HAL；应修改 `.ioc` 后重新生成。
- `ark_dts_generated.c/.h` 由 `../ark_stdio_rust/ark-studio-cli.exe dts.generate --input doc/examples/dts-参数.json
- HAL 只做标准化硬件操作；设备协议、单位换算、校准、业务状态机放在组件或 App。

## 驱动清单（STM32F1）

| compatible / 对象 | 源文件 | 主要职责与 API 形态 |
|---|---|---|
| `ark_hal_gpio` | `stm32f1/src/ark_hal_gpio.c` | `set/get/toggle`、`configure`（输入、推挽、开漏及上拉）、`register_irq`；`ARK_HAL_GPIO_PIN(n)` 将引脚转为位掩码。 |
| `ark_hal_time` | `stm32f1/src/ark_hal_time.c` | `is_ready`、`frequency_hz`、`cycles`、`delay_us`；提供微秒级短延时和周期计数。 |
| `ark_hal_soft_i2c`（无 compatible） | `stm32f1/src/ark_hal_soft_i2c.c` | 软件 I2C `init/transmit`；配置含 `scl/sda`、`delay_us`，可选择先反初始化硬件 I2C。 |
| `ark_hal_uart` | `stm32f1/src/ark_hal_uart.c` + `src/ark_uart_manage.c` | 阻塞/DMA/中断/ DMA-to-idle 收发、`abort`、循环 DMA 检查；manager 统一所有权与 IRQ 观察者。 |
| `ark_hal_i2c` | `stm32f1/src/ark_hal_i2c.c` | 7 位地址（适配层调用 vendor HAL 时统一左移）、阻塞/DMA 收发、8 位寄存器 `mem_read/mem_write`、`recover`、设备探测。 |
| `ark_hal_spi` | `stm32f1/src/ark_hal_spi.c` | 全双工 `transmit/transmit_receive` 及 DMA；按 CubeMX 配置使用 SPI1/2/3。 |
| `ark_hal_can` | `stm32f1/src/ark_hal_can.c` | 标准滤波、loopback、启动、收发和诊断；帧数据最多 8 字节，支持 extended/remote 标志。 |
| `ark_hal_pwm` | `stm32f1/src/ark_hal_pwm.c` | 查询周期、启停、比较值、PWM DMA；通道由 `ark_hal_pwm_channels[]` 绑定。 |
| `ark_hal_adc` | `stm32f1/src/ark_hal_adc.c` | `is_ready` 与带超时 `read(id, channel, uint16_t *)`；当前枚举 ADC1/ADC2。 |
| `ark_hal_encoder` | `stm32f1/src/ark_hal_encoder.c` | 定时器编码器 `start/get_count/set_count`；当前最多两个实例。 |
| `ark_hal_usb_device` | `stm32f1/src/ark_hal_usb_device.c` | CDC 设备 `initialize/is_ready/is_configured/transmit`；`ARK_USB_DEVICE_CUBEMX_INIT` 时由组件 init 调 `MX_USB_DEVICE_Init()`。 |
| `ark_hal_watchdog` | `stm32f1/src/ark_hal_watchdog.c` | `is_ready`、`refresh`；句柄来自生成的 `ark_hal_watchdog_handle`，与 `watchdog` 组件配合。 |

枚举容量是硬约束：I2C 2、ADC 2、SPI 3、UART 3、CAN 1、PWM 4、Encoder 2、Soft-I2C 1。生成器应拒绝越界、重复逻辑资源、未知 compatible、未解析或 disabled provider 引用。

## DTS、provider 与绑定规则

1. 在 `app/<name>/<name>.dts` 声明 `ark_hal_*` controller，使用 `reg`、`status`、label/phandle 等标准属性；缺省 `status` 与 `"okay"` 等价，`"disabled"` 节点不生成、不注册、不进 Keil。
2. 组件通过 `&label` provider 引用 HAL；组件专用属性由组件源码读取和校验，HAL 只接收生成后的强类型编号/句柄。
3. 生成器将 CubeMX handles 映射为 `void *const` 表；适配器不得硬编码 `huart1`、`hi2c1` 等名称，也不得复制 DMA handle、MSP 或 IRQ 实现。
4. 修改 UART/I2C/SPI/PWM 映射时，先改 DTS 与 `.ioc`，再运行：

```powershell
../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe dts.generate --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe --list  # 在工具参数界面填写目标，CLI 使用对应工具 ID 与 JSON 参数
```

5. 检查生成的 `ark_hal_bindings.h`、绑定表和裁剪宏；确认 `hal_catalog.json` 的源码闭包、managed defines 与 Keil 同步一致。

## 并发、DMA、IRQ 与超时

- I2C 阻塞与 DMA 共用同一互斥量；`is_device_ready()`、`recover()` 不能绕过锁。DMA 完成/错误回调必须在可调用 FreeRTOS `...FromISR` API 的优先级下运行（STM32F1 推荐 IRQ priority 5）。
- 只在中断回调中使用 `xStreamBufferSendFromISR`、`vTaskNotifyGiveFromISR` 等 `FromISR` API；ISR 不打印、不执行协议解析、不做长循环。GPIO EXTI 由唯一 `HAL_GPIO_EXTI_Callback()` 分派到 `register_irq()` 注册的组件回调。
- UART 所有权、DMA-to-idle 去重、发送回调和 IRQ 观察者都经 `ark_uart_manage`。`ark_uart_manage_register()` 注册 `ark_uart_t`，`ark_uart_manage_start_receive()` 启动接收，`ark_uart_manage_transmit()` 统一发送。
- `ARK_UART_RX_DMA_TO_IDLE` 优先用于循环 DMA；DMA 通道冲突时改用 `ARK_UART_RX_INTERRUPT`，一字节 HAL 中断接收 + 组件拥有的有界 FreeRTOS StreamBuffer/ring buffer。ISR 只缓冲并通知任务。
- FreeRTOS 任务中使用有限 `timeout_ms`；硬件无响应必须返回失败并保留诊断。不得在 HAL 中永久阻塞或假定 115200 波特率。
- ESP-01 在 STM32F1 无 RX DMA 时：CubeMX 将 USART3 NVIC 设为 priority 4/0；ISR 只能写组件静态 ring buffer，不能调用 FreeRTOS FromISR API（priority 4 不可调用）。

## 典型实现/审查清单

- [ ] 新接口命名为 `ark_hal_*`，类型和对象位于 `hal/include`，错误以 `bool`/`int32_t` 返回并保留超时语义。
- [ ] 适配器只通过 `ark_hal_bindings.h` 取句柄；无 `MX_*_Init`、无重复 `HAL_*_IRQHandler`、无硬编码引脚/波特率。
- [ ] 新驱动和每个源文件已加入 `hal_catalog.json`，没有第二份 manifest。
- [ ] DTS provider、资源 ID、PWM 通道和 CubeMX `.ioc` 一一对应；disabled provider 未被引用。
- [ ] DMA/IRQ 优先级满足 FreeRTOS 约束；ISR 无打印、分配、阻塞和复杂解析。
- [ ] I2C 地址、GPIO active level、ADC 原始值、PWM tick 等单位在 HAL 接口注释/调用方明确。
- [ ] 先运行 `../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json

## 不应做的事

- 不在 `hal/` 引入业务组件、产品算法或字符串设备注册表。
- 不手改 CubeMX 生成的 `Core/Drivers/Middlewares`，不手改 `ark_dts_generated.c/.h`。
- 不让 HAL 在 ISR 中调用普通 FreeRTOS API、打印日志或等待互斥锁。
- 不用 HAL 直接假定某个 App、某个 UART 流名称或旧版服务器模拟遥测。
