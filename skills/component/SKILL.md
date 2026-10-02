---
name: ark-sdk-component
description: 在 ARK CREW SDK 中新增、配置、调试或审查设备组件、组件总线、OF/DTS、CLI/Stream 集成及组件启动流程时使用。
---

# ARK CREW SDK Component Skill

## 何时读取本 Skill

涉及 `component/`、`ark_component`、`ark_of_*`、组件 `register/init/self-test`、DTS 节点、CLI 命令、`ark_stream` 或设备协议时，先读本文件，再核对：

- 新增组件的完整执行规格：`../../doc/components/组件框架规范.md`；执行流程：`../../doc/components/组件添加开发指南.md`；可复制代码模板：`../../doc/component_template/`。

- `doc/current_architecture.md` 与 `doc/ark_sdk_guide.md`；
- `component/common/component_catalog.json`（唯一构建清单）；
- `component/common/ark_component.h`、`ark_dts.h`、`ark_stream.h`、`ark_cli.h`；
- 目标组件同目录 `.h/.c`、相关 App DTS 和生成的 `ark_dts_generated.c/.h`。

文档不覆盖源码事实。简单组件保持 `component/<name>/`；组件类使用 `component/<class>/<class>_<backend>.c/.h`，catalog 通过 `directory` 指向共享类目录。公共实现位于 `component/common/`。组件用户文档统一位于 `doc/components/<name>_开发指南.md`，组件目录禁止放 Markdown。

## 组件总线与 DTS 生命周期

- 每个 catalog 条目 `name` 同时决定 `compatible = "<name>"`、头文件 `<name>.h`、无参数注册函数 `<name>_register(void)` 和裁剪宏 `ARK_DTS_HAS_<NAME>`。
- `register()` 只创建/挂接一个 `ark_component_t` 到全局总线，不执行耗时硬件操作；生成的 `ark_dts_register_components()` 按 DTS 中启用节点调用各注册函数。
- 组件对象可提供 `init`、`self_test`、`self_test_expected_ms` 和 `init_level`（1/2/3）。App 启动任务先按等级串行执行所有非 NULL init，再串行执行 self-test；NULL 回调不执行也不计数。进度总数为两阶段有效回调数之和，失败计入完成但保留失败状态。
- 缺省 `status` 或 `status = "okay"` 表示启用；`status = "disabled"` 节点不得生成、注册、自检或同步进 Keil。引用 disabled provider、未知 compatible、重复资源、未解析 phandle、越界 HAL ID 应由生成器拒绝。
- 若 App 源码仍无条件调用某组件 API，不能只把 DTS 节点设为 disabled；必须同步调整 App 依赖和构建裁剪，否则可能出现链接失败。
- 组件在注册/init 阶段通过 `ark_of_*` 查询自己的 `compatible`/路径及属性，校验后缓存强类型配置（HAL ID、GPIO、地址、波特率、数量等）；实时路径只调用强类型 API，不重复遍历字符串树。
- 常用属性：`compatible`、`status`、`label`、`reg`、`gpios`/专用 `*-gpios`、`pwms`、phandle（`<&provider>`）以及组件定义的 `current-speed`、`ark,*` 属性。属性的必填性和单位以对应组件源码为准；I2C/SPI/UART 等资源 ID 通常由父 provider/phandle 解析，不要臆造 `i2c_id` 之类的 DTS 属性。

## 公共基础 API

- `ark_component_t`、`ark_component_result_t`（`ARK_COMPONENT_OK/ERROR/TIMEOUT`）、初始化等级和进度状态定义在 `ark_component.h`；不要创建新的通用 `/dev` 字符串注册表。
- `ark_of_property_read_bool/u32/s32/u32_array/string/string_index` 读取 DTS；GPIO、HAL provider、父子节点和 phandle 查询也由 `ark_dts` 提供。
- `ark_stream_t` 只暴露 `is_ready/read/write`；消费者通过 `ark_stream_find(name)` 获取流，再调用 `ark_stream_read/write`，不感知 UART、USB、DMA 或 IRQ。一个 stream 默认只能有一个消费型协议组件。
- `ark_cli_register()` 注册静态 `ark_cli_command_t` 链表；`uart_cli` 只负责终端、历史和行编辑，命令应由组件自身注册，未选组件时命令自然不存在。

## 组件目录与能力索引

下表覆盖 `component/common/component_catalog.json` 的组件能力。`HAL` 列是 catalog 依赖；配置属性应从源码/DTS 核对。

| compatible | 源文件 | HAL | 注册与关键 API / 约束 |
|---|---|---|---|
| `adc38_tracking` | `adc38_tracking/adc38_tracking.c` | adc,gpio,time | `adc38_tracking_register/read/scan_raw/set_calibration/capture_white/capture_black`；固定 8 通道，样本含 raw/normalized、position、valid_mask、tick；`settle_us` 和每通道采样次数影响时序。 |
| `adc_sensor` | `adc_sensor/adc_sensor.c` | adc | `adc_sensor_register/read(kind, sample)`；支持 `MQ7`、`MQ135`、`SOIL`，返回 16 位 raw、mV 和 tick；实例由 ADC ID+channel 配置。 |
| `beidou` | `beidou/beidou.c` | 无 | `beidou_register/get_position`；通过父级 `stream` 读取 NMEA，校验 RMC/GGA XOR 并输出 `latitude_e7/longitude_e7`。 |
| `bh1750` | `bh1750/bh1750.c` | i2c | `bh1750_register/read`；运行时 I2C ID 由父 provider 解析，DTS 使用 `reg=<0x23>`；当前源码地址常量固定为 `0x23` 且不读取 `reg`，若使用 `0x5C` 必须先改代码；输出 `lux_x10`（0.1 lux）和 tick。 |
| `buzzer` | `buzzer/buzzer.c` | gpio | `buzzer_register/set/beep`；配置单 GPIO，`beep(duration_ms)` 使用毫秒，勿在 ISR 调用。 |
| `bluetooth` | `bluetooth/bluetooth.c` | 无 | `bluetooth_register/packet_send/get_control`；通过父级 `stream` 处理固定包头、累加校验和小端组包。 |
| `control` | `control/control.c` + `control_pid.c` + `control_line_tracking.c` | 无 | 核心管理策略、速度闭环和 motor；PID 是纯算法，循迹是内置策略，避障等通过 `control_strategy_t` 扩展。 |
| `dht11` | `dht11/dht11.c` | gpio,time | `dht11_register/read`；DTS 使用 `data-gpios`，组件负责开漏输出/输入切换与微秒协议时序；返回 `temperature_deci_c`、`humidity_deci_percent`，仅发布校验和正确的新样本。 |
| `flash` | `flash/flash.c` | 无 | 统一设备对象和读写擦除接口。 |
| `flash_stm32f103` | `flash/flash_stm32f103.c` | 无 | 512 B 逻辑区、1 KiB 物理擦除页，通过 flash 核心访问。 |
| `flash_w25q16` | `flash/flash_w25q16.c` | gpio,spi | 2 MiB、4 KiB 扇区的 SPI NOR 后端。 |
| `icm20602` | `icm20602/icm20602.c` | gpio,i2c | `icm20602_register/wait_data_ready/read_sample/calibrate/update/get_motion/get_diagnostics/scan`；支持 0x68/0x69 扫描，原始采样、单位换算、零偏、互补滤波在组件内完成；自检后可创建内部采样任务，App 读取快照而非自行 `update`。 |
| `json` | `json/json.c` + `json/cJSON.c` | 无 | `json_register/initialize/create_object/parse/delete/get_*/add_*/print`；基于 cJSON，调用方负责内存生命周期和输出缓冲容量。 |
| `lcd` | `lcd/lcd.c` + `lcd/st7789.c` | gpio,spi | `lcd_register/clear/fill_rect/draw_*/printf/refresh_region/get_diagnostics`；固定 240x320 RGB565，内部条带缓冲避免分配整屏 153600 B；区域刷新回调完成绘制后经 SPI DMA 提交。 |
| `maixcam` | `maixcam/maixcam.c` | 无 | `maixcam_register/get_result/get_command/send_response`；通过父级 `stream` 处理 5A 5B 长度帧，保留原始字节读取和任务就绪通知。 |
| `led` | `led/led.c` | gpio | `led_register/init/self_test/set_by_id/toggle_by_id/count`；支持多实例 `reg`+`gpios`，自检与启动进度关联；ID 越界必须返回失败。 |
| `encoder` | `encoder/encoder.c` | encoder | `encoder_register/reset/get/encoder_delta_to_rpm10`；左右通道映射 HAL encoder ID，带 polarity 与 counts_per_revolution；速度换算单位 `rpm10`。 |
| `motor` | `motor/motor.c` | 无 | 统一占空比、电流和停止接口，只允许一个后端。 |
| `motor_tb6612` | `motor/motor_tb6612.c` | gpio,pwm | TB6612 双路 PWM/方向后端。 |
| `motor_can` | `motor/motor_can.c` | can | CAN/M3508 电流后端与回环自检。 |
| `servo` | `servo/servo.c` | gpio,pwm,time | `servo_register/set_angle/set_pulse_us/reset/disable`；角度 0..180°，脉宽单位微秒；支持硬件 PWM 或软件 PWM，切换 transport 需匹配 DTS provider。 |
| `oled` | `oled/oled.c` + `oled/ssd1306.c` | i2c | `oled_register/printf/canvas_clear/draw_*/present/copy_frame`；固定 128x64、1024 B framebuffer；`dirty_refresh` 仅提交变化列，App 每帧绘制完成后只调用一次 `oled_present()`。 |
| `wifi` | `wifi/wifi.c` | 无 | AP 连接和 TCP/UDP/TLS socket，不解释上层协议。 |
| `wifi_esp_at` | `wifi/wifi_esp_at.c` | 无（经 stream） | ESP8266/ESP32 AT 后端，只翻译链路命令。 |
| `ark_net` | `ark_net/ark_net.c` + `ark_net_protocol.c` | 无 | 会话与 ARK JSONL 协议分离，底层只调用 wifi socket。 |
| `stream` | `stream/stream.c` | uart | `stream_register`；扫描多个 `stream` 节点，每个实例独占一个物理 UART，支持 `ark,rx-mode=interrupt|dma|dma-to-idle`，通过 `ark_stream_find` 提供 transport-neutral 字节流。 |
| `uart_cli` | `uart_cli/uart_cli.c` | 无 | `uart_cli_register/start/write`；配置流名称与欢迎文本；CLI 启动应在组件初始化后显式调用，重任务放到独立 FreeRTOS task。 |
| `usb` | `usb/usb.c` | usb_device | `usb_register`；配置流名称，后端使用 CubeMX USB CDC；电脑尚未枚举时 CLI 应等待重试，不修改 `usb_device.c/usbd_cdc_if.c`。 |
| `watchdog` | `watchdog/watchdog.c` | watchdog | `watchdog_register/request_reset`；与 `ark_hal_watchdog` 配合；启用 IWDG 时必须在长初始化前启用 HAL controller 和该组件，避免重复复位。 |
| `ws2812b` | `ws2812b/ws2812b.c` | gpio,pwm,time | `ws2812b_register/init/self_test/write/set/fill/off`；最多 8 颗，颜色顺序 GRB；支持 PWM DMA 或软件时序。STM32F1 TIM3_CH4 PWM DMA 与 SPI1 TX 共用 DMA1 Channel 3 时，必须让 PWM 独占该通道。 |

## 配置与注册工作流

1. 在单一 DTS `app/<name>/<name>.dts` 添加节点，先确认 provider compatible、label/phandle、`status` 和资源不冲突；不要新增 App JSON 作为第二配置源。
2. 按组件源码所需属性填写 `reg`、`gpios`/`*-gpios`、`pwms`、I2C/SPI/UART provider、地址、`current-speed`、数量或单位参数。多实例使用 `name@N` 并用 `label` 区分。
3. 运行生成器检查并生成：

```powershell
python -m studio.cli dts app/<name> --check
python -m studio.cli dts app/<name>
```

4. 检查 `ark_dts_generated.c/.h` 中组件裁剪宏、配置对象、HAL 绑定和 `ark_dts_register_components()` 顺序；生成文件不可手改。
5. 在 `appStartTask()` 中保持：读取 `/software` -> `ark_dts_register_components()` -> `app_component_boot_run()`；注册函数只挂总线，初始化/自检由统一启动任务串行执行。
6. 组件若提供 CLI 命令，在 `<name>_register()` 中调用 `ark_cli_register()`；数据通道优先复用 `ark_stream`，不得把 UART/DMA 细节泄漏到协议层。物理 UART 只能由 `stream` 注册，协议组件不得直接调用 `ark_uart_manage_register()`。

## 资源、时序与失败处理

- 所有硬件访问使用 HAL 的超时 API；组件必须在 init/self-test 中可返回，不能永久阻塞 CubeMX 启动任务。
- I2C 设备共用 HAL 互斥量；OLED/ICM20602/BH1750 等组件不得绕过 `ark_hal_i2c` 直接调用 vendor HAL。
- 中断回调只做有界缓存和任务通知；禁止打印、动态分配和复杂解析。DHT11 的 GPIO 时序、WS2812B 的 800 kHz 波形、LCD/OLED 的帧提交必须在任务上下文完成。
- 组件 init 或 self-test 失败时保留失败状态和诊断，不能因此抑制 UART CLI 或其他已成功组件启动；失败操作计入进度完成数。
- 任何新增组件都必须声明完整 `sources` 与 `hal` 依赖；同一 C 源不得出现在多个 catalog 条目，非法或不存在文件由生成器报错。

## 新增组件的固定交付物

新增组件必须同时提交：

1. 简单组件使用 `component/<name>/<name>.h/.c`；类子组件使用 `component/<class>/<name>.h/.c` 并填写 catalog `directory`。
2. `component/common/component_catalog.json` 唯一条目。
3. 对应 App DTS 节点及必要的 provider/phandle 绑定。
4. 至少一项协议/配置/边界测试；硬件不可在 CI 访问时，使用纯解析或 mock 测试覆盖。
5. `doc/components/<name>_开发指南.md`，完整说明 DTS、每个公共 API、使用流程、注意事项和验证方法。

建议从 `doc/component_template/` 复制起步；不要复制现有组件的私有缓存、任务句柄或历史兼容宏。

## 审查清单与验证

- [ ] 目录名、compatible、头文件和 `<name>_register()` 一致；无第二份 manifest。
- [ ] 所有 DTS 属性在源码中有明确读取、类型/范围校验和默认策略；单位（`x10`、`deci_c`、`permille`、微秒、毫秒）写入接口或注释。
- [ ] disabled 节点不会生成/注册；HAL provider、GPIO、PWM、总线 ID 和实例数量均在容量范围内。
- [ ] 组件不手改 `ark_dts_generated.c/.h`、CubeMX 生成文件或 IRQ；需要硬件变更时修改 DTS/.ioc 后重新生成。
- [ ] 自检耗时有界，任务栈足够；高负载 AT/HTTP、传感器融合等工作由独立动态任务承担。
- [ ] 运行 `python -m studio.cli dts app/c8t6_microcar_soil --check`、`python -m studio.cli project audit-paths ..`，并执行相关 `studio/tests`；固件改动再做 Keil full rebuild。

## 明确禁止

- 不恢复已移除的 generic `common_dev` 字符串 `/dev` 注册表。
- 不把控制策略写成独立硬件组件；循迹、避障等通过 control 策略接口输出命令。
- 不在 Wi-Fi 后端实现 HTTP/MQTT/设备 JSON 等组包；协议必须位于 net 组件源码。
- 不在组件中复制 HAL 句柄、定义 IRQ、硬编码 CubeMX 引脚或绕过 `ark_stream`/`ark_uart_manage`。
- 不因可选组件失败而跳过 CLI；不在 ISR 中调用阻塞 API、普通 FreeRTOS API 或 `printf`。
