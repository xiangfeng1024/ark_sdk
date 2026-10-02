# ARK CREW Embedded SDK 开发与使用手册

## ESP AT 与 ARK 网络协议示例

`c8t6_ark_net` 是独立网络联调 App：LCD 继续占 SPI1+DMA，CLI 使用 USART1，
ESP-01 使用 USART3 PB10/PB11、115200 和 RX 中断。不得给 USART3_RX 配 DMA，
因为 STM32F103 的 DMA1 Channel3 已由 SPI1_TX 使用。

网络链路位于 `component/wifi` 与 `component/ark_net`：`wifi_esp_at` 实现 ESP AT 后端，`wifi` 暴露标准 socket，`ark_net_protocol` 负责业务组包，`ark_net`
负责 TCP/JSONL 遥测和 LED 命令；`component/json` 是仅含 cJSON 核心的 JSON 包装层。
Wi-Fi凭据和设备密钥由 DTS 配置，CubeMX 仍负责外设、DMA 与 NVIC。

该 App 的 DHT11 节点为 `dht11 { data-gpios = <&gpiob 5 0>; };`。DHT11 组件在读取时
把 PB5 切换为开漏输出/输入单线时序，App 每 2 秒采样一次；只有校验和正确的采样才会
更新 LCD 与 HTTP 遥测。`ark_net` 在首次有效采样前不发遥测，因此远端旧数据不能
作为本次实机结果。DHT11 数据线必须有可靠上拉，且传感器 VCC/GND 与 MCU 共地。

> 当前架构权威快照：[`current_architecture.md`](current_architecture.md)。新开发统一采用 `c8t6_microcar_soil` 验证过的“同名 CubeMX/Keil 工程 + 单 DTS + 集中组件/HAL 目录 + 生成式 OF”链路。本文保留的旧 App、旧 JSON/JSONC 配置和历史硬件排查记录仅用于追溯；若与权威快照或当前源码冲突，以当前源码和权威快照为准。

当前文档入口：

- 架构与源码总览：[`current_architecture.md`](current_architecture.md)
- 脚本和 Studio 使用：[`script_usage.md`](studio_tooling.md)
- 当前产品需求：[`../app/c8t6_microcar_soil/PROJECT_REQUIREMENTS.md`](../app/c8t6_microcar_soil/PROJECT_REQUIREMENTS.md)

## 1. 框架边界

工程分为四层：

```text
app/<name>/<name>.dts + App业务源码
  -> ark_dts_generated.c/.h + ark_dts OF只读运行时
    -> 可复用组件 / ark_stream / uart_manage
      -> ark_hal_gpio、ark_hal_uart、ark_hal_i2c等全局驱动对象
        -> DTS根compatible选中的平台适配层
          -> CubeMX生成句柄与厂商HAL
```

职责约定：

- CubeMX 负责芯片引脚、时钟、外设参数、DMA、NVIC 和默认任务配置。
- SDK 不修改 CubeMX 生成的 `Core`、`Drivers`、`Middlewares` C/H 文件。
- DTS生成/同步脚本根据启用节点的compatible和status选择SDK源码、HAL、头文件目录与裁剪宏。
- app 和 SDK 组件统一使用 FreeRTOS 原生接口；CubeMX 生成层可以继续使用 CMSIS-RTOS v2。
- 每次 CubeMX 重新生成后，都要再次生成DTS C/H并同步Keil，因为 `.uvprojx` 可能被重建。
- App 负责业务流程、任务调度、动画、渲染算法和项目状态机；一个 App 可以由 `app/<name>/src` 下多个源文件组成，同步器会递归加入全部源文件。
- HAL 只做统一硬件接口到平台库的适配；组件只实现可复用的设备与基础功能。LCD/OLED 组件可以提供画点、画线、局部/全局刷新和字符串显示，但三维投影、旋转、FPS 策略等必须留在 App。

组件采用扁平目录，避免无意义的 `include/src` 嵌套：

```text
component/
  common/
    ark_component.c
    ark_component.h
  lcd/
    lcd.c
    lcd.h
    st7789.c
    st7789.h
```

简单组件仍放在 `component/<name>/`。类组件可在 `component/<class>/` 聚合核心、后端和算法，catalog 通过 `directory`、`requires`、`sources` 和 `hal` 保持独立裁剪；不增加第二份 manifest。

新增组件的统一规格见 [`组件框架规范.md`](../doc/components/组件框架规范.md) 和 [`组件添加开发指南.md`](../doc/components/组件添加开发指南.md)，可复制代码骨架位于 `component_template/`。新组件必须独立源码目录、集中式 `<name>_开发指南.md`、独立公共头文件、无参数 `<name>_register(void)`、私有 context、DTS 节点、catalog 条目和至少一项边界测试。

## 组件生成器

可使用组件生成器从 `doc/component_template/` 创建组件，并写入 central catalog 条目：

```powershell
../ark_stdio_rust/ark-studio-cli.exe --list  # 在工具参数界面填写目标，CLI 使用对应工具 ID 与 JSON 参数
../ark_stdio_rust/ark-studio-cli.exe --list  # 在工具参数界面填写目标，CLI 使用对应工具 ID 与 JSON 参数
```

生成器不会修改任何 App DTS。仍需将组件节点加入 App DTS，再运行 DTS 生成和完整校验清单。

## 2. 历史 CubeMX 配置与硬件排查记录

> 本节主要记录旧 `c8t6_demo`、双屏、USB、IMU 等阶段的实测证据，不是当前 `c8t6_microcar_soil` 的完整配置清单。当前项目配置以同名 `.ioc`、DTS 和 `PROJECT_REQUIREMENTS.md` 为准。

当前 `c8t6_demo.ioc` 已正确配置：

```text
系统时钟             72 MHz
USART1 TX/RX         PA9 / PA10
USART1               921600，8 位，1 停止位，无校验
USART1 TX DMA        DMA1 Channel 4，Normal
USART1 RX DMA        DMA1 Channel 5，Circular
SPI1                 Master，1-Line半双工发送，Mode 3，8 bit，MSB，Soft NSS
SPI1 SCK/MOSI        PA5 / PA7
SPI1 时钟            18 MHz（APB2 72 MHz / 4）
SPI1 TX DMA          DMA1 Channel 3，Normal
DMA IRQ 优先级       5
```

USART1 全局中断现已在 CubeMX 中启用，抢占优先级为 5、子优先级为 0。生成的 `stm32f1xx_it.c` 已包含 `USART1_IRQHandler()`，并正确调用 `HAL_UART_IRQHandler(&huart1)`。

PC13 当前的 `GPIO output level` 是 Low。板载 LED 为低电平点亮，这会导致 LED 在 GPIO 初始化后先亮、等组件初始化时才熄灭。建议在 CubeMX 将 PC13 初始输出改为 High，使上电默认状态与 LED 组件的“关闭”状态一致。

如果后续重新配置 UART，必须保留 USART1 全局中断。没有该中断时，DMA 半满和满中断仍可能工作，但 TX DMA 完成回调和短帧 UART IDLE 事件无法走完整 HAL 链路。

当前SPI1 TX DMA为High优先级，USART1 RX/TX DMA为Low优先级。双屏动画和921600 UART CLI并发实测正常；如果后续UART持续大流量接收并出现溢出，再在CubeMX中提高USART1 RX DMA优先级，不要仅凭通道数量明确指定存在仲裁故障。

SPI1 当前已是1-Line半双工主机发送模式，PA6未被占用。SPI1 TX DMA为DMA1 Channel 3、Normal、字节宽度、内存递增、High优先级；DMA1 Channel 3和SPI1全局中断优先级均为5。

### IWDG、I2C1 与 I2C2 配置

当前 `.ioc` 已包含 IWDG 和 I2C1，并已重新生成对应代码。I2C1 与 IWDG 均符合以下配置基线：

IWDG：

```text
Peripherals -> IWDG -> Activated
Prescaler: 64
Reload Counter: 625
目标超时：约 1 秒（LSI 实际频率有偏差）
```

CubeMX 应生成 `Core/Inc/iwdg.h`、`Core/Src/iwdg.c`、全局 `hiwdg`，并在 `main.c` 启动调度器前调用 `MX_IWDG_Init()`。

I2C1：

```text
I2C1 Remap
PB8  -> I2C1_SCL
PB9  -> I2C1_SDA
I2C Speed Mode: Fast Mode
Clock Speed: 400000 Hz
Addressing Mode: 7-bit
Dual Address / General Call / No Stretch: Disable
```

DMA/NVIC：

```text
I2C1_TX -> DMA1 Channel 6, Memory Increment, Byte, Normal, High
DMA1 Channel 6 IRQ: priority 5/0
I2C1 Event IRQ:     priority 5/0
I2C1 Error IRQ:     priority 5/0
```

当前 SH1106 已验证使用 CubeMX 生成的硬件 I2C1，`.ioc` 为 400 kHz Fast Mode。OLED 命令和显示数据都使用阻塞 HAL 接口，不调用 DMA；每个页面或脏子区域使用一次 `0x40 + 连续显示数据` 的阻塞事务。SPI1 仍是独立的 18 MHz 预留接口，与当前四线 I2C OLED 无关。

双屏App中OLED曾从30 FPS下降到约24 FPS。原因是旧 `oled_present()` 每帧固定发送8个完整Page：包含定位和控制字节后约1080字节，在400 kHz I2C上仅线速就需要约24 ms；再叠加高优先级LCD任务的抢占，OLED单帧超过33 ms周期。当前实现逐Page比较现有帧与上一次成功画面，只发送首个至最后一个变化列。旋转线框通常只覆盖约60列，因此显著缩短阻塞I2C占用。实机连续两次导出Framebuffer均解码为 `FPS:30`，约1秒内成功提交序号增加34，确认OLED已恢复30 FPS。

CubeMX 应生成 `Core/Inc/i2c.h`、`Core/Src/i2c.c`、全局 `hi2c1`，并在 `main.c` 中先 `MX_DMA_Init()`、再 `MX_I2C1_Init()`。不要手工修改这些生成文件。

MicroCar 的 ICM20602 使用独立硬件 I2C2：PB10=SCL、PB11=SDA、400 kHz Fast Mode、7位地址、不使用DMA。传感器输出高电平推挽的50 us Data Ready脉冲，因此PB0配置为上升沿EXTI、下拉、NVIC优先级5，在脉冲开始时立即唤醒采样任务。`.ioc` 与生成的 `gpio.c` 已核对一致。生成代码同时包含 `hi2c2`、`MX_I2C2_Init()`、`EXTI0_IRQHandler()`、`I2C2_EV_IRQHandler()` 和 `I2C2_ER_IRQHandler()`，后两个IRQ调用对应HAL处理函数。


## 3. DTS唯一配置源与Keil同步

`c8t6_microcar_soil`是当前维护和验收的DTS App。所有工程身份统一使用`<版型前缀>_<项目名>`：App目录、DTS、CubeMX目录、IOC、Keil工程和Keil Target必须完全同名。STM32/GD32 F103C8T6的前缀由平台清单确定为`c8t6`。

历史拼写错误的Cube工程`c8t6_mircocar_soil`已移入Windows回收站；当前工作区只保留并自动绑定`ark_stm32_projects/c8t6_microcar_soil`。

根节点包含两个同级区域：

```dts
/dts-v1/;

/ {
    model = "ARK CREW MicroCar Soil";
    compatible = "c8t6_microcar_soil", "stm32f103c8";

    sys {
        /* CubeMX控制器、总线、GPIO/PWM/ADC引用和组件实例 */
    };

    software {
        /* tasks、control、line-follow、display、lighting等运行时策略 */
    };
};
```

DTS不再保存PC构建元数据。工具根据根compatible取得版型前缀，验证App名，并自动查找同名IOC、Keil工程和Target；SDK相对路径自动计算，Reset-and-Run默认开启。`/software`中的所有启用节点都是MCU可读取的运行时策略。

常用命令：

```powershell
../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe dts.generate --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe --list  # 在工具参数界面填写目标，CLI 使用对应工具 ID 与 JSON 参数
```

固定生成文件为`include/ark_dts_generated.h`和`src/ark_dts_generated.c`，禁止手工修改。它们包含无堆只读OF表、CubeMX句柄与PWM通道绑定、DTS推导的裁剪宏，以及自动注册组件的`ark_dts_register_components()`。

组件 DTS `compatible` 使用 catalog 名称，例如 `dht11`、`motor_tb6612`、`flash_w25q16`。类核心作为父节点，后端作为子节点；生成器校验父子关系、后端数量、requires 和资源冲突。

生成器只读取`component/common/component_catalog.json`，不解析C源码。每项`name`同时决定`compatible=<name>`、同目录`<name>.h`、`<name>_register()`和`ARK_DTS_HAS_<NAME>`；`sources`明确列出该组件需要同步到Keil的全部C源文件，`hal`声明驱动依赖。每个非common组件C文件必须在目录中恰好出现一次，遗漏、重复归属、非法/重复组件名、越界或不存在的源码都会直接报错。

HAL同样只维护`hal/common/hal_catalog.json`。它按平台集中声明根compatible、Keil芯片Profile、版型前缀和全部驱动；每个驱动声明DTS compatible、完整SDK源码闭包、CubeMX工程源码、编译宏和受管理宏。每个`hal/<platform>/src`及`hal/src`源文件必须恰好归属于一个驱动。生成器还会拒绝未知启用设备compatible、重复逻辑资源、未解析phandle、disabled provider引用和HAL容量越界。组件专用属性的含义、必填性和类型由组件源码负责校验，初始化失败会进入组件自检/启动错误流程。

### 3.1 旧JSON链说明（仅历史资料）


每个 App 现在有两份职责明确的配置：

- `app.json`：选择平台、HAL驱动、组件和Keil工程信息。
- `sys_config.jsonc`：只声明会随App或硬件连线变化的内容，例如GPIO、有效电平、逻辑外设编号、CubeMX生成句柄和组件实例。固定地址、超时、任务栈、内部缓冲区与通用自检时序由对应组件源码统一维护。

修改 `sys_config.jsonc` 后运行：

```powershell
```

生成结果是：

```text
app/dual_display/include/app_sys_config.h  App专用宏
app/dual_display/src/app_sys_config.c      HAL句柄表和类型化组件配置对象
```



相比把所有资源生成成 `ARK_GPIO_BL` 一类全局宏，当前实现优先生成有类型的配置对象：

```c
extern const lcd_config_t ark_lcd_config;
extern const led_config_t ark_led_config;
extern const oled_config_t ark_oled_config;
extern const icm20602_config_t ark_icm20602_config;
extern const uart_cli_config_t ark_uart_cli_config;
```

这样可以表示多个LED实例、检查GPIO/外设类型，并避免宏名称冲突。App自身的明确选项放在 `application` 对象中；当前五个App用 `application.status_led_id` 生成 `ARK_APP_STATUS_LED_ID`。旧的通用 `symbols` 对象已删除，避免配置退化成含义不明确的宏集合。

JSONC顶部的 `$schema` 只是告诉IDE使用哪份JSON Schema，以提供中文说明、自动补全和错误标记；它不会进入固件，也不参与运行。配置说明直接使用 `//` 或 `/* ... */` 注释放在字段外部，不再加入 `_comment` 数据字段。生成器优先读取 `sys_config.jsonc`，暂时兼容旧 `sys_config.json` 作为迁移入口。

`active_level` 表示逻辑有效电平，不是上电默认电平：`0` 表示低电平有效，`1` 表示高电平有效。例如LED配置为0时，`led_set_by_id(id, true)` 会输出低电平。上电默认输出仍由CubeMX的GPIO初始电平配置负责。

LCD和多LED配置示例：

```json
"components": {
  "lcd": {
    "spi": "spi1",
    "BL": {"pin": "PA1", "active_level": 1},
    "CS": {"pin": "PA2", "active_level": 0},
    "DC": {"pin": "PA3", "active_level": 1},
    "RST": {"pin": "PA4", "active_level": 0}
  },
  "led": {
    "instances": [
      {"id": 0, "gpio": {"pin": "PC13", "active_level": 0}},
      {"id": 5, "gpio": {"pin": "PB0", "active_level": 1}}
    ]
  }
}
```

LED逻辑ID不要求连续；`led_set_by_id(5, true)` 会查找生成的实例表，而不是把5当数组下标。

HAL适配层通过生成的 `ark_hal_i2c_handles[]`、`ark_hal_spi_handles[]`、`ark_hal_uart_handles[]` 和 `ark_hal_watchdog_handle` 查找CubeMX句柄。因此切换到 `spi2/hspi2` 或 `uart2/huart2` 时，只需CubeMX生成对应句柄并修改App配置，不需要修改STM32F1适配器源码。

如果工程启用了SPI1和SPI2，`hal.spi.instances` 中就写两项，分别映射 `hspi1` 和 `hspi2`。这张表不会初始化或运行时注册全部SPI；CubeMX负责初始化，LCD等组件只会取自己配置的 `spi` 项。

注意：系统配置负责“选择和绑定”，不替代CubeMX硬件初始化。比如JSON把LCD映射到PA2，`.ioc` 中仍必须把PA2配置为正确输出模式；JSON选择UART2时，CubeMX仍必须生成 `huart2`、DMA和NVIC配置。

`app/blinky/app.json` 当前选择 STM32F1、GPIO、硬件 I2C、Watchdog、UART，以及 LED、OLED、Watchdog 和 UART CLI 组件。`keil.device` 和 `keil.reset_and_run` 也属于项目配置：

```json
"keil": {
  "project": "../../../ark_stm32_projects/c8t6_demo/MDK-ARM/c8t6_demo.uvprojx",
  "target": "c8t6_demo",
  "sdk_link": "../../ark_sdk",
  "device": "STM32F103C8",
  "reset_and_run": true
}
```

```powershell
../ark_stdio_rust/ark-studio-cli.exe --list  # 在工具参数界面填写目标，CLI 使用对应工具 ID 与 JSON 参数
../ark_stdio_rust/ark-studio-cli.exe --list  # 在工具参数界面填写目标，CLI 使用对应工具 ID 与 JSON 参数
```

UART 已由 CubeMX 生成，所以 UART manifest 不再把 `stm32f1xx_hal_uart.c` 加入 SDK Vendor 组。同步器会清理旧 SDK 添加的 `HAL_UART_MODULE_ENABLED` 命令行宏，使用 CubeMX 在 `stm32f1xx_hal_conf.h` 中生成的宏，避免重复源码和宏重定义警告。

## 4. HAL 全局驱动对象

HAL 层不使用运行期平台注册。公共头文件直接声明：

```c
extern const ark_hal_gpio_driver_t ark_hal_gpio;
extern const ark_hal_uart_driver_t ark_hal_uart;
extern const ark_hal_i2c_driver_t ark_hal_i2c;
extern const ark_hal_spi_driver_t ark_hal_spi;
extern const ark_hal_watchdog_driver_t ark_hal_watchdog;
```

STM32F1 的 `ark_hal_uart.c` 不再直接出现 `huart1`。它从App生成的句柄表取得CubeMX句柄，不重复定义UART/DMA句柄，也不初始化引脚、波特率、DMA或NVIC。I2C、SPI和Watchdog适配器采用相同模型。

I2C统一接口除原始阻塞/DMA收发外，还提供8位寄存器地址的 `mem_read()`/`mem_write()` 和 `recover()`；ICM20602用一次14字节寄存器突发读取获得加速度、温度和角速度。STM32F1恢复实现会在SDK互斥锁保护下执行 `HAL_I2C_DeInit()`/`HAL_I2C_Init()`。`is_device_ready()`也必须使用同一把互斥锁，不能让CLI扫描与正常传输同时进入厂商HAL。GPIO统一接口提供 `register_irq()`，STM32适配层在唯一的 `HAL_GPIO_EXTI_Callback()` 中按EXTI线分派到组件回调，组件无需修改CubeMX生成的IRQ文件。

同一个 app JSON 只能选择一个平台实现；如果错误地编译两个平台的同名全局对象，链接器会报告重复符号。

## 5. Stream 与 UART Manage

`ark_stream` 是面向上层协议和CLI的通用字节流接口。消费者只保存一个按名称查找到的 `ark_stream_t`，不感知UART编号、DMA、USB端点或底层中断：

```c
ark_stream_t *stream = ark_stream_find("debug");

ark_stream_write(stream, data, size, 1000U);
ark_stream_read(stream, data, size, ARK_STREAM_WAIT_FOREVER);
```

公共对象提供 `is_ready/read/write` 操作以及 `ark_stream_attach()`、`ark_stream_detach()`、`ark_stream_find()` 注册表。当前 `stream` 组件实现多个 UART 后端实例，内部每个实例统一拥有：

- 一个向 `uart_manage` 注册的 `ark_uart_t` 子对象。
- 64字节 DMA/中断接收区；中断模式另有 128 字节无锁环形缓冲。
- 256字节FreeRTOS Stream Buffer。
- DMA TX完成信号量和发送互斥量。

这些传输资源已经从 `uart_cli.c` 移出。以后增加USB CDC或HID时，应创建新的 `ark_stream_t` 后端并挂入相同注册表；CLI、命令解析、历史记录和行编辑代码不需要修改。

每个使用CLI的App在 `app.json` 同时选择 `stream` 和 `uart_cli`，在 `components_init()` 中先调用 `stream_register()`、再调用 `uart_cli_register()`。当前UART绑定示例：

```json
"stream": {
  "name": "debug",
  "uart": "uart1",
  "baud_rate": 921600
},
"uart_cli": {
  "stream": "debug"
}
```

生成器会验证流名称一致，并把UART硬件编号与波特率只生成到 `ark_stream_config`。`uart_cli_config` 仅保存流名称与欢迎文本。

### UART Manage 子对象模型

协议组件不再直接维护 `ark_uart_t`。只有 `stream` 组件为每个物理 UART 维护一个 `ark_uart_t`：

```c
static ark_uart_t camera_uart = {
    .name = "camera",
    .uart_id = ARK_HAL_UART_2,
    .tx_mode = ARK_UART_TX_DMA,
    .rx_mode = ARK_UART_RX_DMA_TO_IDLE,
    .rx_buffer = camera_rx_buffer,
    .rx_buffer_size = sizeof(camera_rx_buffer),
    .irq_callback = camera_uart_irq,
    .irq_context = NULL,
};
```

基本使用流程：

```c
ark_uart_manage_register(&camera_uart);
ark_uart_manage_start_receive(&camera_uart);
ark_uart_manage_transmit(&camera_uart, data, size, timeout_ms);
```

规则：

- 同一个硬件 UART 同时只允许注册一个主子对象。
- 子对象记录名称、UART ID、收发方式、接收缓冲区和主 IRQ 回调。
- `ark_uart_manage_register_irq()` 可额外注册观察者，例如统计或协议监控模块。
- 平台 HAL 回调只负责把 TX 完成、RX 半满、RX 完成、RX Idle 和错误事件送入 `uart_manage`。
- Circular DMA 下，manager 根据上次 DMA 位置拆分新增数据并处理缓冲区回绕，不会在每次 Idle 后错误地重启仍在运行的 DMA。
- Normal DMA-to-idle 下，manager 在 Idle/完成事件后自动重新挂接接收。
- UART 错误发生后，manager 通知监听者、终止当前传输并重新启动 RX。
- IRQ 回调中只能调用 FreeRTOS 的 `...FromISR` 接口，不能调用 `printf`。

以后新增摄像头、串口陀螺仪等组件时：先在 CubeMX UI 配置对应 UART 的引脚、波特率、DMA 和 NVIC，再在 DTS 中添加 `stream` 节点和具体协议子节点。协议组件只获取 `ark_stream_t`，不复制 IRQ Handler；一个物理 UART 默认只允许一个 stream 和一个消费型协议组件。

## 6. 组件总线与自检

每个组件有五个总线属性：

```c
typedef struct {
    const char *name;
    ark_component_action_t init;
    ark_component_action_t self_test;
    uint32_t self_test_expected_ms;
    ark_component_init_level_t init_level;
} ark_component_t;
```

组件提供无参数注册接口，例如：

```c
watchdog_register();
uart_cli_register();
led_register();
oled_register();
icm20602_register();
```

公开注册接口统一使用 `<name>_register()`，不再包含 `component` 字样。注册接口只把组件对象挂到全局组件总线。`init_level` 取1、2、3；`ark_component_init_all()` 在App启动任务中按等级顺序串行调用全部非NULL的init，随后 `ark_component_self_test_all()` 在同一个任务中串行调用全部非NULL的self-test，不再创建动态worker、任务队列或调度上下文。需要承担启动界面的OLED为一级且在MicroCar首个注册；Watchdog和UART CLI为二级，其余设备为三级，因此OLED仍最先初始化。

MicroCar严格先通过 `ark_component_init_all(progress_report)` 串行完成全部有效init，之后才调用 `ark_component_self_test_all(report, progress_report)` 串行执行全部有效self-test。App将两阶段合并为一条等权总进度：总任务数等于非NULL init回调数量加非NULL self-test回调数量，每完成一个回调就增加相同百分比。回调为NULL的操作不执行、也不计数。失败仍计入完成数但不计入成功数，并闪烁失败组件名。初始化阶段的 `FINISHED` 事件仅记录init成功数，不进入完成页面；只有最终自检完成才显示汇总并转入运行界面。`appBoot` 绘制确认使用独立二值信号量。完成后显示聚合成功数/总数500 ms，然后居中显示 `Runing`，每500 ms增加一个点，依次为0、1、2、3个点，满3点后重新开始。

MicroCar把完整启动逻辑集中在 `app/microcar/src/app_component_boot.c`，公开入口 `app_component_boot_run()` 声明在 `app/microcar/include/app_component_boot.h`。该模块统一维护init/self-test执行、合并进度、OLED绘制、失败闪烁、串口自检日志、appBoot任务和运行提示动画。`app_main.c` 的 `components_init()` 只保留组件注册；`appStartTask()` 注册完成后调用启动入口，再创建正常App任务。新增App源文件由JSON同步器递归加入Keil工程。

串行组件总线不再分配自检调度上下文、动态worker或队列，避免占用CubeMX默认启动任务栈和FreeRTOS heap。由于self-test直接运行在启动任务中，组件自身必须保证操作可返回；耗时硬件接口继续使用各自HAL超时。

LED组件不再包含具体引脚。每个App的 `sys_config.jsonc` 可以声明任意数量的LED实例，为每个实例配置不要求连续的逻辑ID、GPIO和有效电平。当前配置把ID 0映射为低电平有效的PC13；三次闪烁和600 ms预算属于通用自检策略，由LED组件维护。

ICM20602组件负责寄存器初始化、Data Ready同步、原始数据读取、单位换算、陀螺仪零偏标定、互补滤波欧拉角、角加速度计算和诊断统计，因为这些算法与传感器量程、采样率及坐标轴定义紧密绑定。自检通过后，组件自行动态创建384-word采样融合任务，完成静止标定后持续调用内部更新流程，并通过临界区保护的全局快照向外提供数据。App不得再循环调用 `icm20602_update()`，只使用 `icm20602_get_motion()` 读取快照。组件保留 `icm20602_read_sample()` 原始采样接口，并提供 `icm20602_scan()` 扫描0x68/0x69和关键寄存器。OLED排版、3D模型与显示任务仍属于App。

## 7. App 启动流程

CubeMX 默认动态任务的函数名是弱定义 `appStartTask`。SDK app 强定义同名函数，因此无需 `StartDefaultTask` 跳板：

```text
CubeMX 动态创建 appStartTask
  -> components_init()
       -> 注册 Watchdog、UART CLI、LED 和 OLED
       -> ark_component_init_all()
       -> ark_component_self_test_all()
  -> taskENTER_CRITICAL()
  -> xTaskCreate(appLed)
  -> xTaskCreate(appOled)
  -> taskEXIT_CRITICAL()
  -> vTaskDelete(NULL)
```

`components_init()` 是独立公开接口，统一完成全部组件的注册、初始化和超时自检。没有额外的 `app_component_task`。组件处理结束后，`appStartTask` 才在临界区内创建所有 app 周期任务。所有 SDK 任务都使用 `xTaskCreate` 动态创建。

LED 周期任务在组件初始化和自检完成后创建，之后每秒切换一次 PC13：

```c
TickType_t next_wakeup = xTaskGetTickCount();

while (1) {
    vTaskDelayUntil(&next_wakeup, pdMS_TO_TICKS(1000U));
    led_toggle_by_id(0U);
}
```

OLED 周期任务同样动态创建，每秒在 `(x=0, page=6)` 使用 16 点阵显示当前 FreeRTOS Tick：

```c
while (1) {
    oled_printf(0U, 6U, 16U, "%lu", (unsigned long)xTaskGetTickCount());
    vTaskDelayUntil(&next_wakeup, pdMS_TO_TICKS(1000U));
}
```

当前组件组合要求 CubeMX 的 FreeRTOS `TOTAL_HEAP_SIZE` 至少为 8192 字节。实测 4096 字节时，组件自检结束后剩余 1776 字节，创建 `appLed` 后剩余 1136 字节，随后 256-word 栈的 `appOled` 创建失败。改为 8192 字节后，两个 app 动态任务创建完成后仍剩余 4080 字节。必须检查每次 `xTaskCreate()` 的返回值；不要通过忽略失败或改为静态任务掩盖动态堆不足。

### Cube3D App

`app/cube3d` 是独立的3D线框立方体示例，复用与blinky相同的Watchdog、Stream、UART CLI、LED和OLED组件。它保留PC13 LED每秒翻转任务，并动态创建384-word栈的 `appCube` 任务。选择和同步该App：

```powershell
```

立方体使用8个顶点、12条边、Q10定点旋转矩阵和5度步进正弦表，不依赖Cortex-M3软浮点三角函数。为保持类似参考图的斜视立方体轮廓，X轴固定俯视20度，Y轴从25度开始并每帧旋转5度，72帧完成一个360度循环；这避免首帧正对观察方向时只看到重合轮廓。显示采用正交投影而不是近大远小的透视投影，因此前后两个面保持相同尺寸、三组平行边在屏幕上仍保持平行，不会呈现棱台感。投影中心为 `(50,35)`，Q10坐标每单位缩放20像素；全周期坐标范围约为 `x=22..78、y=7..63`，立方体整体向左留出右上角FPS区域。

任务用33 ms的 `vTaskDelayUntil()` 周期瞄准30 fps。FPS不是固定文本：只统计成功提交到OLED的帧，每500 ms使用 `成功帧数 × configTICK_RATE_HZ / 实际Tick数` 更新一次；首个统计窗口显示 `FPS:--`，之后以8点阵 `FPS:xx` 显示在右上角 `(x=80, page=0)`。若存在OLED提交失败，每个统计窗口最多输出一条汇总串口日志，便于区分渲染问题与I2C刷新问题。

每帧只进行一次OLED提交：先清空内存画布，绘制投影后的12条边和FPS文字，最后调用 `oled_present()`。提交函数逐Page与上一次成功画面比较，只发送该Page从首个变化字节到最后变化字节的连续区间。不要让每条线单独触发I2C刷新，否则无法稳定达到30 fps。Cube3D与blinky都强定义 `appStartTask`，JSON同步器必须保证Keil的 `ARK_SDK/App` 同一时间只包含其中一个。

### Dual Display App

`app/dual_display` 同时选择 GPIO、I2C、SPI、Watchdog、UART HAL，以及 LCD、LED、OLED、Watchdog、UART CLI组件。它保留PC13 LED每秒翻转任务和Cube3D的OLED 30 FPS正交投影任务，另建384-word栈的LCD任务，在240×320 ST7789上以黄色RGB565显示循环旋转的立体长方体和实时FPS。

`dual_display/sys_config.jsonc` 的板级映射为：BL=PA1、CS=PA2、DC=PA3、RST=PA4，组件使用SPI1。BL/DC高电平有效，CS/RST低电平有效。SPI1 SCK/MOSI由CubeMX配置为PA5/PA7。LCD和ST7789源码不再出现这些具体端口、引脚或SPI编号。当前 `.ioc` 与生成的 `gpio.c` 已确认PA1..PA4均为推挽输出，初始电平依次为Low、High、Low、High，与硬件连接一致。

不能为240×320 RGB565分配整屏 framebuffer，因为单帧需要 `240 × 320 × 2 = 153600` 字节，远超20 KB SRAM。LCD组件只维护1152字节RGB565大端传输缓冲，并提供 `lcd_clear()`、`lcd_fill_rect()`、`lcd_draw_pixel()`、`lcd_draw_line()`、`lcd_printf()`、`lcd_refresh_region()` 以及条带surface基础绘图接口。`lcd_refresh_region()` 根据区域宽度自动计算条带高度，逐条带回调App渲染器后通过SPI DMA发送，因此既能局部刷新，也能用同一接口完成全屏刷新。

加入 `lcd_hw` 诊断后，当前链接结果为Flash 47868/65536（剩余17668）和SRAM 19312/20480（剩余1168）；SRAM统计已包含8192字节FreeRTOS heap。OLED动画直接复用现有1024字节帧缓冲，局部百分比字符缓冲仅5字节，不增加静态SRAM。STM32F103C8实机重新烧录后LCD与OLED均为30 FPS，LCD DMA失败数为0，动态heap观测2544字节，任务stack high-water mark为LED 104、OLED 250、LCD 244 words。

实机优化前LCD只有约7 FPS，暂停OLED后约13 FPS，DMA失败计数始终为0。根因不是DMA通道仲裁，而是4行条带渲染时，每个条带都重复遍历12条完整棱边。按条带独立裁剪虽然提升到30 FPS，但每个条带重新用整数交点初始化Bresenham，丢失了上一条带的误差相位，某些角度会在条带边界出现断线。

最终实现对每条棱边只做一次全视口裁剪，并维护12个流式Bresenham迭代器；`x/y/error`跨条带持续推进，每条边整帧只遍历一次。该算法、正交投影、64级蓝—青—绿—黄—红深度渐变和渲染诊断全部位于 `app/dual_display/src/app_lcd_3d.c`，组件不知道“长方体”或“FPS”的业务含义。迭代器使用App静态非重入工作区，避免占用任务栈。App每帧只重绘144×144长方体区域和72×14 FPS区域，约发送43488字节。最终实机保持30 FPS、DMA失败数0，OLED任务保持开启；LCD任务优先级为`tskIDLE_PRIORITY + 2`，其余App任务为`+1`。

长方体线条使用高跨度冷暖深度渐变增强立体感。顶点在完成Y轴旋转和20度X轴倾斜后保留相机Z深度，将固定的`-1664..1664`深度范围映射到64级RGB565色谱：远端蓝色，依次经过青色、绿色、黄色，近端为红色。每条边只在初始化时计算一次Q16颜色步进，随后颜色相位与Bresenham的`x/y/error`一起跨条带推进。逐像素阶段只有定点加法、移位和查表，没有浮点、除法或HSV转换；两个像素宽度使用相同颜色。渐变状态使12个静态迭代器工作区增加96字节，但每帧SPI数据和40次DMA事务不变。实机验证仍为30 FPS、DMA失败数0。

### MicroCar App

`app/microcar` 选择GPIO、I2C、SPI、PWM、CAN、UART、Watchdog HAL，以及ICM20602、LED、OLED、W25Q16、WS2812B、Motor、UART CLI、Watchdog组件。它默认只创建PC13 LED每秒翻转任务；ICM20602由组件自己的任务持续采样和解算，启动进度结束后OLED清屏，不默认显示IMU数据。可将App内 `APP_IMU_DISPLAY_ENABLED` 改为1启用仅负责读取快照和排版的显示任务，该任务不访问I2C、不调用传感器更新。切换方案使用：

```powershell
```

传感器配置为200 Hz内部采样（`SMPLRT_DIV=4`）、约41 Hz陀螺仪低通、约44.8 Hz加速度计低通、±500 dps和±4 g。ICM20602不会通过I2C直接输出欧拉角：I2C读取的是三轴加速度、三轴角速度和温度原始寄存器。组件上电保持静止采集64个Data Ready样本计算陀螺仪零偏；随后在组件内部用陀螺仪积分与加速度计倾角构成互补滤波，得到Roll/Pitch。Yaw没有磁力计或外部航向参考，只能积分Z轴角速度；组件使用静止零角速度更新抑制静止漂移，但运动中的绝对航向仍会长期漂移。角加速度也不是传感器直接给出的，而是组件用相邻角速度之差除以实测Tick间隔计算；加速度计读数包含重力，不能仅凭六轴IMU可靠分离任意运动下的重力与平移加速度。

OLED仅显示 `Roll`、`Pitch`、`Yaw` 三个欧拉角，每项占用一行8×16字体，分别放在Page 1、3、5，保留上下各8像素留白。标签从同一x坐标开始并使用固定5字符左对齐字段，显示为 `Roll `、`Pitch`、`Yaw  `，因此标签起点、冒号和数值起点分别固定在同一列；角度采用带正负号的 `Name :+ddd.dd` 格式，不显示单位。FPS、角速度、角加速度、加速度、温度和错误统计仍可通过 `imu` CLI查看，不再挤占屏幕。FreeRTOS整数Tick无法把33.333 ms表示为固定周期，因此任务循环使用33/33/34 Tick的分数累加，长期平均保持30 Hz；ICM20602仍按200 Hz采样和融合，降低显示帧率不会降低姿态计算更新率。

Yaw静止补偿采用带迟滞的零角速度更新：进入静止要求三轴补偿后角速度绝对值不超过2 dps、加速度模长位于0.85～1.15 g，并连续满足100个样本（0.5秒）；已静止时采用5 dps与0.75～1.25 g的退出阈值，并要求连续10个异常样本（50 ms）才确认运动，避免单样本尖峰误触发。静止锁定后只使用有效静止样本以约5.1秒时间常数在线跟踪三轴零偏，并把角速度积分量置零；运动确认后立即恢复正常积分。该策略会忽略低于进入阈值且无法被加速度变化识别的极慢旋转，这是六轴静止检测的固有限制，可根据小车最低有效转速再调整阈值。

补偿前静止6分钟基线中，Yaw约从4.65°漂至10.65°，漂移约+6.0°（约+0.98°/min），温度约28.3～28.8°C，固定Z零偏为393 mdps。补偿后再次静止6分钟，Yaw始终为0.06°，变化0.00°；温度覆盖约28.18～28.76°C，原始Z角速度出现约−2.72到+2.96 dps尖峰，静止迟滞仍保持锁定。`imu` 输出原始角速度、在线零偏、补偿后角速度、静止状态和连续静止样本数，便于后续针对实际车辆振动调整参数。

原厂ICM-20602的 `WHO_AM_I` 应为 `0x12`，当前实物在地址0x69返回 `0x2E`，但关键寄存器布局、量程、Data Ready中断和连续采样均已验证兼容。组件接受这两个值以支持当前模块，同时在启动日志对非0x12值明确告警，不能据此把该器件标记为原厂ICM-20602。曾捕获到IRQ继续增长但I2C2不再应答、读取失败持续增加的状态；修复设备探测绕过互斥锁的问题，并让原始采样在首次读取失败后恢复I2C2再重试，同时用 `recover` 计数记录恢复事件。Yaw补偿版固件占用Flash 47680/65536（余17856）和SRAM 18016/20480（余2464）。此前I2C修复实机连续运行至约3.8万次姿态更新并通过3次连续 `imu_scan`；Yaw补偿版又完成6分钟静止测试，期间FPS 30、静止状态持续有效且Yaw变化0.00°。后续一分钟复查中采样数从7984增至20338、IRQ从8660增至21014，Yaw保持0.20°，`read_fail/timeouts/recover`均为0，空闲heap 3696字节，任务stack high-water mark 346 words。

Flash 驱动统一放在 `component/flash`。`flash` 核心提供设备查找、容量、读写与擦除接口；`flash_w25q16` 和 `flash_stm32f103` 是独立裁剪后端。W25Q16 使用 SPI1 Mode 0 和低有效 CS，业务通过统一 flash API 访问。

启用 `flash_stm32f103` 子节点时，工程同步器按 `ark,reserve-bytes` 将 STM32F103C8 链接 IROM 缩小到 `0xFC00`。App 通过 `flash_find("internal")` 和统一 flash API 访问末尾 512 字节逻辑区。

WS2812B组件支持最多8颗灯。MicroCar通过TIM3_CH4/PB1的800 kHz PWM DMA发送GRB码流，`microcar_soil`通过PA7软件时序直接发送且不保留像素帧缓冲。默认 `ws2812b_self_test()` 将全部8颗灯以白色127亮度点亮200ms，随后即使点亮失败也尝试发送全零帧；注册对象启用该自检，因此它计入启动进度。`ws2812 <0..7>` 将纯白亮度线性映射到0..127。72 MHz定时器采用PSC=0、ARR=89，逻辑0高电平约25 Tick、逻辑1约50 Tick。TIM3_CH4与SPI1_TX都固定映射DMA1 Channel 3，因此PWM DMA App必须删除SPI1 TX DMA，SPI Flash只用阻塞全双工，Channel 3专供TIM3_CH4。

Motor 使用核心加后端：`motor_tb6612` 提供左右 PWM/方向输出，`motor_can` 提供 M3508 四路电流和回环自检。上层统一调用 `motor_set_duty_permille()`、`motor_set_currents()` 和 `motor_stop()`，不再识别后端类型。

CAN 后端使用 CubeMX 生成的中断和 HAL 接口；ISR 不打印或执行电机业务逻辑。需要反馈帧时应在 `motor_can` 后端增加有界队列和强类型快照，而不是让 control 直接访问 CAN。

CAN HAL仍保留状态、错误码、bxCAN ESR/TSR/FIFO1和软件队列诊断接口，供后续App使用。此前普通总线出现REC/LEC异常和发送邮箱占满，最终确认是CAN收发器模块供电不足：当前模块必须接5 V电源，同时与MCU、USB-CAN共地。

MicroCar实机历史验证确认：`help` 只列出通用命令和 `imu_scan`；组件内 `imu_scan` 在0x69读取到WHO=2E、PWR1=01、DIV=04、CFG=03、GYRO=08、ACCEL=08、ACCEL2=03、INTCFG=00、INTEN=01。CAN模块改接5 V后的普通模式和0x1FF中断结果有效。当前版本已移除并行组件调度；LED注册回调仍为NULL，WS2812B则注册200ms全白后熄灭的自检回调。

### Aircraft3D App

`app/aircraft3d` 复用MicroCar的ICM20602、OLED、LED、UART CLI、Watchdog和同一套项目级硬件映射，并保留PC13每秒翻转任务。3D航模由14个顶点、24条棱线组成，包含机身、主翼、平尾、垂尾、座舱和机腹；模型与投影算法单独放在App的 `app_aircraft_3d.c`，OLED组件只负责画线与提交帧缓冲，ICM20602组件只负责采样和姿态解算。

每个顶点先按实时欧拉角依次执行Roll-X、Pitch-Y、Yaw-Z旋转，再使用视角Yaw −20°、俯仰+25°的固定斜视正交相机投影，以9倍比例围绕OLED中心 `(64,32)` 显示。IMU继续以200 Hz更新，显示采用33/33/34 Tick分数周期保持平均30 FPS；完整模型画入内存后每帧只调用一次 `oled_present()`。切换与烧录：

```powershell
```

`aircraft` CLI命令输出实际FPS、失败帧、IMU更新与IRQ次数、I2C读取失败/恢复次数、当前欧拉角、静止状态、剩余heap和任务栈水位。实机首次验证达到30 FPS，渲染失败、I2C读取失败和恢复次数均为0，帧数与IMU更新数持续增长，空闲heap为3696字节，任务stack high-water mark为352 words。最终全量编译为0错误、0警告，Flash使用48252/65536（余17284），SRAM使用18016/20480（余2464），DAP下载完成后Verify OK并自动运行。Yaw来自六轴IMU的陀螺仪积分，没有磁力计绝对航向参考，因此运动中的长期航向漂移仍是系统限制。

## 8. UART CLI

UART CLI 查找名为 `debug` 的 `ark_stream_t`。当前该流由 `stream` 组件的USART1后端提供：

```text
921600，8N1
Circular DMA + UART Idle 不定长接收
DMA 发送
128 字节 DMA RX 缓冲区
256 字节 FreeRTOS Stream Buffer
```

Circular DMA 的 HT、TC、Idle 回调都由 `uart_manage` 转换成“不重复的新数据段”，再由stream后端写入自己的Stream Buffer。CLI任务只调用 `ark_stream_read()`，不再包含UART回调或自行重启DMA。

CLI 自检把完整欢迎信息作为一次 DMA 发送，并检查 DMA 完成结果；发送失败时返回 `ARK_COMPONENT_ERROR`，成功后才启动 CLI 任务。支持：

```text
help
hello
led <id> <0|1>
oled_fb
lcd_diag（仅dual_display App）
lcd_hw（仅dual_display App，读取MCU、时钟、GPIOA、SPI1、DMA1通道3及NVIC诊断信息）
oled_pause（仅dual_display App，暂停OLED动画以隔离性能问题）
oled_resume（仅dual_display App，恢复OLED动画）
imu（仅microcar App，显示姿态、采样、错误、heap和任务栈诊断）
imu_scan（仅microcar App，扫描0x68/0x69并读取关键配置寄存器）
aircraft（仅aircraft3d App，显示3D渲染、欧拉角与IMU运行诊断）
reset
```

`printf` 通过 `ark_stream_write()` 发送；当前UART后端使用UART1 DMA，DMA TX完成和错误由manager唤醒等待发送的任务。以后切换USB流时无需修改CLI和 `printf` 重定向。

`oled_fb` 用于显示链路诊断。它导出的不是正在被绘制的工作缓冲，而是最后一次成功刷新后保存的完整画面，避免 CLI 与渲染任务同优先级时抓到半帧。输出格式为：

```text
OLED_FB_BEGIN width=128 height=64 format=page-lsb size=1024 sequence=<N>
0000: <16 bytes hex>
...
03F0: <16 bytes hex>
OLED_FB_END fnv1a32=<CHECKSUM>
```

`sequence` 每次成功提交画面后递增；连续执行两次可判断画面是否仍在刷新。1024字节采用SH1106帧缓冲的Page-LSB布局：偏移为 `page * 128 + x`，每个字节bit 0..7依次对应该Page内从上到下的8个像素。

`dual_display` 强定义CLI扩展命令 `lcd_diag`，把LCD组件的初始化、区域提交、DMA失败和累计字节统计，与App的成功帧序号、实际FPS、角度、8个投影顶点、FreeRTOS heap及任务栈水位组合输出。`lcd_hw` 位于App调试层，读取CPU、RCC、FLASH、AFIO、GPIOA、SPI1、DMA1 Channel 3、HAL句柄和NVIC原始状态，用于比较STM32F103与GD32F103板卡，不向LCD基础组件加入芯片逻辑。针对STM32显示异常依次测试了PA1～PA4高速输出、SPI降至9 MHz以及禁用DMA改用阻塞发送，画面均未恢复；将SPI从Mode 0改为Mode 3后画面正常，因此最终配置恢复为18 MHz、DMA、高优先级LCD任务和低速控制脚，只保留Mode 3。最终寄存器为`CR1=0xC34F`，DMA外设地址`0x4001300C`、IRQ优先级5、HAL错误0、LCD 30 FPS。按用户要求，Mode 3目前直接写在CubeMX生成的`spi.c`中，`.ioc`仍保留Mode 0；以后CubeMX重新生成会覆盖该修复，届时必须重新应用Mode 3。STM32F103x8/B用户程序读取`DBGMCU_IDCODE`为0符合ST勘误中“DBGMCU寄存器仅在调试模式可访问”的限制，不能单独据此判断芯片真伪。`oled_pause`/`oled_resume` 用于隔离OLED阻塞I2C刷新对系统性能的影响。LCD采用实机验证过的启动顺序：先关闭BL，完成复位、Sleep Out和控制器配置后发送Display ON（此时BL仍关闭），再用DMA把整屏GRAM清黑，等待20 ms，最后打开BL。关键约束是清屏必须早于背光点亮。LCD组件自检随后显示 `LCD OK`，不执行任何三维业务逻辑；运行任务由App接管动画。

CLI 支持常用行编辑：

- `↑`：调出上一次执行的命令。
- `↓`：恢复按上键前正在输入的草稿。
- `←` / `→`：移动逻辑光标。
- 在光标中间输入会插入字符并重绘后半行。
- Backspace 删除光标前一个字符，因此可以左移到错误参数处再删除、重新输入。
- 同时兼容终端发送的 `ESC [ A/B/C/D` 和 `ESC O A/B/C/D`。

`reset` 先等待提示文本 DMA 发送完成，再停止 IWDG 刷新。Watchdog 组件平时每 250 ms 喂狗；停止喂狗后约 1 秒发生硬件复位。该方式依赖 CubeMX 已在 `main()` 中初始化 IWDG。

### UART 不打印排查

STM32F1/GD32F103 的 HAL DMA TX 完成分为两段：DMA1 Channel 4 完成后，HAL 打开 USART 的 Transmission Complete 中断；随后必须由 `USART1_IRQHandler()` 调用 `HAL_UART_IRQHandler(&huart1)`，才会进入 `HAL_UART_TxCpltCallback()` 并释放 CLI 的发送信号量。缺少 USART1 全局中断时，CLI DMA 发送会超时，自检会失败，DMA-to-idle 的短帧接收也无法工作。

`uart_cli` 的任务由 `uart_cli_self_test()` 发出的任务通知启动，因此 App 启动流程必须在
其它可选组件（LCD、DHT11、ESP-01 等）初始化或自检失败后继续执行全部自检；不能因某个
外设失败而在通知 CLI 前删除启动任务。串口无欢迎信息、`help` 无响应时，优先检查这一启动
门控关系，其次检查 USART1 DMA1 Channel4/5 与 USART1 IRQ、PA9/PA10 交叉接线及 COM3 的
921600、8N1 配置。

Studio“串口收发监视器”的波特率默认值为 `auto`：它从当前 App DTS 的
`uart_cli -> stream -> current-speed` 自动推导，`c8t6_ark_net` 因而会自动使用
COM3（或用户指定端口）的 921600。端口、波特率和启动命令会按工作区/App记忆；非空启动命令自动追加CRLF。只有诊断其它设备时才手动输入明确波特率。

网络 App 的 CLI 自检只负责启动交互终端；耗时的 ESP-01 入网与 HTTP 事务必须由专用网络
任务完成。`wifi at` 是最小物理链路诊断：输出 `wifi at: OK` 才能继续检查 SSID、服务器和
鉴权；`wifi at: no response` 表示应先检查 ESP-01 的 3.3V 稳定供电、共地、PB10→ESP RX、
PB11←ESP TX、EN/CH_PD 拉高及模块 AT 固件的 115200 波特率。

ESP-01 使用 USART3 的中断接收而不使用 DMA（DMA1 Channel3 已保留给 SPI1 LCD）。
在 CubeMX 的 **NVIC Settings** 中必须把 **USART3 global interrupt** 设为抢占优先级
**4**、子优先级 **0**，然后重新生成工程。该 ISR 在优先级 4 只能写 Wi-Fi 组件的静态
环形缓冲，不能调用任何 FreeRTOS FromISR API；优先级 5 会被 FreeRTOS 临界区屏蔽，
在 115200 baud 的连续 AT\\r\\n\\r\\nOK\\r\\n 响应下触发 HAL_UART_ERROR_ORE 并导致丢包。
生成后应先运行 wifi diag，确认 errors=0，再运行 wifi at；正确字节显示为
41 54 0D 0A 0D 0A 4F 4B 0D 0A，并输出 wifi at: OK。
wifi diag 会持久显示最近一次入网失败原因；ESP-AT 的 +CWJAP:3 表示找不到目标 AP。
此时检查 DTS 的 SSID、ESP-01 的 2.4 GHz 覆盖范围及路由器是否开启 SSID 广播，而不是修改
串口参数或服务器配置。

网络联调完成的判据是 wifi state=2、net diag 的 fail=0，并且服务端仪表盘的 temperature_x10、
humidity_x10 和 sequence 持续更新。网页或 API 写入 LED 命令后，运行 net poll；net status 的
led/revision 必须与服务端一致。

网页 LED 控制使用服务器 .env 中的 WEB_CONTROL_KEY，密钥不写入静态页面或 SDK。首次点击会提示
输入；如果浏览器保存的是旧密钥，服务端返回 401 时页面会自动清除旧值、重新提示并重试一次。
重启或重新部署网页后出现短暂 502 属于容器启动期，等待 healthz 返回 ok 后再操作。

当前 ATK 复合调试器使用 CMSIS-DAP 烧录，并把串口映射为 COM3。完成 USART1 NVIC 修正后，已实测：

- STM32F103C8完整Profile同步后，DAP擦除、编程、校验成功并出现 `Application running...`。CubeMX重新生成后仍应再次执行SDK同步；命令行日志没有 `Application running...` 时，固件可能已写入但CPU仍处于停止状态。
- COM3 以 921600、8N1 收到完整欢迎信息。
- `uart_cli` 自检 `PASS 3 ms`，LED 三闪自检 `PASS 541 ms`。
- `help` 返回完整命令列表，`hello` 返回 `hello, ark!`。
- 连续发送 140 字节、跨越 Circular DMA HT/TC 边界时仍能收到并解析，证明 DMA-to-idle、回绕增量管理和 PC→MCU 接收方向均正常。
- IWDG 使用 Prescaler 64、Reload 625；发送 `reset` 后约 1.0 秒重新启动。
- watchdog、UART CLI、LED 和 OLED 已完成板级验证。SH1106 最终稳定方案为硬件 I2C 400 kHz、不使用 DMA、每页或脏区域作为一个连续阻塞数据包，同时保留初始化后全屏清零和后续局部刷新。DMA版黑屏，但审计未发现通道映射、DMA优先级、NVIC优先级或IRQ处理配置错误，因此不再为该低带宽OLED继续启用DMA。FreeRTOS heap 为8192字节，`appLed` 与 `appOled` 均可创建。

## 9. I2C HAL、Watchdog 与 OLED

`ark_hal_i2c` 使用 7 位从机地址，对 STM32 HAL 调用时由适配层统一左移。阻塞收发和 DMA 收发共用互斥量；DMA API 等待 HAL 完成/错误回调，因此 I2C/DMA IRQ 必须设置为可调用 FreeRTOS `...FromISR` API 的优先级 5。

SH1106 OLED 当前约定：

```text
面板：0.96 英寸 128x64
驱动：SH1106（内部 `ssd1306.*` 文件名暂为历史兼容名称）
I2C 地址：0x3C（7 位）
总线：I2C1，PB8/PB9 Remap，400 kHz Fast Mode
命令：每条命令一个阻塞硬件 I2C 事务
数据：每个脏页使用一个 `0x40 + 连续区域数据` 阻塞硬件 I2C 事务
显存：1024 字节页格式帧缓冲
刷新：上电全屏清零一次，之后默认局部刷新
```

OLED 初始化等待上电稳定200 ms，初始化命令、页/列命令顺序和零列偏移继续采用已经点亮实物的参考序列。初始化完成后调用一次 `ssd1306_clear()`，逐页定位并清零全部显存。之后默认局部刷新：即时字符只刷新自身8列和实际占用的1或2页，画布提交则逐Page比较前后帧并刷新变化列范围；每个受影响页面都重新发送Page、Column High和Column Low，再发送该页连续区域。当前版本不调用 `transmit_dma()`。

OLED组件自检只调用I2C `is_device_ready()` 探测固定7位地址0x3C，不再承担任何启动动画。真实启动进度属于App逻辑：MicroCar在全部组件初始化完成后先提交0%页面，再根据组件总线的Tick进度回调刷新页面；界面没有 `Loading` 文本，只保留居中的百分比、进度条和当前自检组件名。这样OLED组件仍只关心显示基础能力，不包含具体产品启动流程，也不增加第二帧缓冲。

局部刷新并不是SH1106的强制要求。实物已经验证：400 kHz硬件I2C阻塞方式下，全屏刷新也正常，只要拆成8个Page，每页先重新设置 `(0, page)`，再发送 `0x40 + 128字节`；不能把SH1106当成SSD1306水平寻址模式直接连续发送1024字节。早期黑屏不能归因于“全屏刷新”或“没有设置零点”，因为当时同时改变了DMA、初始化和分包方式；最终对照测试证明，决定性差异是传输路径，阻塞发送下局部与全屏都可用。默认选择局部刷新是为了减少I2C占用和任务阻塞时间，而不是因为全屏刷新不兼容。

OLED DMA审计结果：I2C1 TX映射到DMA1 Channel 6，Normal模式、内存地址递增、外设/内存字节宽度、High硬件优先级均正确；SPI1 TX、USART1 RX/TX均为Low，因此不存在其他通道压制OLED DMA的配置。DMA1 Channel 6、I2C1 EV/ER的NVIC优先级均为5，且生成的IRQ分别调用 `HAL_DMA_IRQHandler()`、`HAL_I2C_EV_IRQHandler()` 和 `HAL_I2C_ER_IRQHandler()`。DMA版自检能够收到HAL完成回调，说明DMA完成和I2C收尾链路已运行；现有证据不足以把黑屏归因到优先级仲裁。若未来必须定位，需要逻辑分析仪同时对比阻塞版和DMA版最后一个数据字节、BTF及STOP附近的SCL/SDA波形；当前OLED数据量很小，保留阻塞实现更稳妥。

格式化显示接口：

```c
int oled_printf(
    uint8_t x,
    uint8_t y,
    uint8_t font_size,
    const char *format,
    ...);

oled_printf(0U, 0U, 8U, "voltage=%u", voltage_mv);
oled_printf(0U, 2U, 16U, "RPM:%d", rpm);
```

- `x` 范围为 0-127。
- `y` 是页坐标，范围为 0-7；16 点阵占两页，因此起始页最大为 6。
- `font_size` 只接受 8 或 16，分别使用 6×8（8 像素字格）和 8×16 ASCII 点阵。
- 每个字符按 8 像素宽前进，超出右边界自动换页；返回已格式化并显示的字符数，失败返回 -1。
- 接口会更新帧缓冲并立即刷新字符串实际覆盖的局部区域，不应在中断中调用。

动画和批量图形使用“画布+提交”接口：

```c
oled_canvas_clear();
oled_draw_pixel(x, y, true);
oled_draw_line(x0, y0, x1, y1);
oled_draw_printf(80U, 0U, 8U, "FPS:%2lu", fps);
oled_present();
```

前四个接口只修改内存中的1024字节帧缓冲，不产生I2C事务；`oled_present()` 才逐页比较并提交变化列范围。这样可以先完成一整帧绘制，再一次显示，避免绘制过程中的撕裂和重复总线传输。坐标超出128×64时，像素和直线绘制会在写帧缓冲时自然裁剪。

直线使用标准Bresenham算法。每个像素循环必须先保存同一个 `2 * error`，再分别判断X/Y方向；不能让Y方向判断读取已经被X方向修改的error。旧实现对部分斜率不会收敛，导致Cube3D任务永久卡在第一帧绘线阶段：组件自检Logo一直保留、提交序号停在2、FPS也不会出现。修复后误差和增量使用32位整数，所有线段能够收敛并进入整帧提交。

Watchdog组件设为二级初始化；OLED作为一级组件先完成约200 ms的硬件初始化，随后Watchdog启动周期刷新，仍在当前约1秒IWDG窗口内。其自检执行一次 `HAL_IWDG_Refresh()`；CLI reset只改变组件刷新状态，不直接操作芯片复位寄存器。

Watchdog动态任务必须把 `xTaskCreate()` 返回的句柄保存到组件自己的持久变量，不能使用 `NULL` 输出句柄，以便明确管理任务生命周期并支持后续诊断。

## 10. 独立开发工具

工具现由 Rust Studio 提供，位于独立仓库，支持 GUI 与 CLI。SDK、App、工程、Target、Keil 独立选择；任务冻结本次参数，工具不得从其它选择明确指定工程。

DTS 校验与生成无需 Keil 工程；编译仅需要工程、Target 与 Keil。同步、配置和打包在执行时检查兼容性，只修改明确目标。具体输入、工具创建规范与命令见 [开发工具说明](studio_tooling.md)。

## 11. 编译与烧录

```powershell
../ark_stdio_rust/ark-studio-cli.exe --list  # 在工具参数界面填写目标，CLI 使用对应工具 ID 与 JSON 参数
```

烧录只在用户明确要求且硬件已连接时执行。

## 12. 需要用户操作时的协作规则

当后续目标必须依赖 agent 无法完成的物理动作，例如按硬件复位键、重新插 USB、连接探针/模块、改变供电、修改 `.ioc` 或在 CubeMX 重新生成时：

1. agent 先完成所有不依赖该动作的代码和检查。
2. 明确列出用户要做的设置、原因及完成标志，然后暂停并询问用户。
3. 用户回复完成后，agent 重新读取 `.ioc`、生成文件、Keil 元数据或设备连接状态。
4. 从原计划未完成步骤继续，不重复已经完成的工作。
5. 不通过手改 CubeMX 生成文件绕过用户操作。

## 13. USB CDC 与 usb_debug App

`app/usb_debug` 是最小 USB 调试方案，只选择 GPIO、USB Device、Watchdog HAL，以及 LED、USB、Watchdog、CLI 组件，不引入 UART、OLED、I2C 或显示任务。PC13 保留每秒翻转任务；`help`、`hello`、`led <id> <0|1>` 和 `reset` 通过 USB CDC 与电脑交互。

为单独验证SDK移植性，现已从 `ark_stm32_projects/c8t6_demo` 克隆出 `ark_stm32_projects/c8t6_usb_cdc`。副本保留CubeMX源码、Drivers、Middlewares和Keil工程定义，不复制Keil输出目录、调试缓存或用户界面文件。CubeMX工程、Keil target和输出名都已重命名为 `c8t6_usb_cdc`；只有 `usb_debug/app.json` 使用新工程，其它App继续使用原来的 `c8t6_demo`。两个Cube工程都直接使用相对路径访问仓库 `ark_sdk`，不依赖带本机绝对目标的目录联接。

后续克隆其它CubeMX工程可直接启动图形界面，选择源工程、目标父目录、输入工程名并一键克隆：

```powershell
../ark_stdio_rust/ark-studio-cli.exe --list  # 在工具参数界面填写目标，CLI 使用对应工具 ID 与 JSON 参数
```

等价命令行为：

```powershell
../ark_stdio_rust/ark-studio-cli.exe --list  # 在工具参数界面填写目标，CLI 使用对应工具 ID 与 JSON 参数
  ark_stm32_projects\c8t6_demo `
  ark_stm32_projects\c8t6_usb_cdc `
  --name c8t6_usb_cdc `
  --app-config ark_sdk\app\usb_debug\app.json
```

目标目录必须不存在，脚本不会覆盖已有工程。默认把SDK引用写成相对路径；只有明确传入 `--sdk-junction` 时才建立目录联接。先加 `--dry-run` 可以只检查源工程、目标、名称、SDK路径和App配置。由于不同MCU和CubeMX版本的 `.ioc` middleware键并不稳定，脚本不会通过文本替换伪造USB配置；在对应版本CubeMX UI生成后，用审计命令检查 `.ioc`、HAL宏、初始化调用、中断链和CDC文件：

```powershell
../ark_stdio_rust/ark-studio-cli.exe --list  # 在工具参数界面填写目标，CLI 使用对应工具 ID 与 JSON 参数
```

通用 `ark_stream_t` 接口及注册表位于 `component/common`。UART 的 `stream` 组件和 USB 的 `usb` 组件只是两个后端：前者维护 UART DMA-to-idle，后者把 CDC OUT 中断数据投递到 256 字节 FreeRTOS StreamBuffer，并用互斥锁串行执行 CDC IN。CLI 只按名称查找 `debug`，因此不包含 UART 或 USB 的实现细节。只有 App 选择 OLED 时才编译 `oled_fb` 命令。USB 上电时电脑可能尚未枚举完成，CLI 任务会等待并重试欢迎信息，连接成功后再显示提示符。

`usb_debug/sys_config.jsonc` 将 `hal.usb_device` 映射到 CubeMX 生成的 `hUsbDeviceFS`，并把 `components.usb.name` 与 `components.uart_cli.stream` 都设为 `debug`。SDK 使用 CubeMX 自带的 ST USB Device CDC middleware，适配器在组件初始化时接管 CDC interface 回调；不要修改生成的 `usb_device.c`、`usbd_cdc_if.c` 或中断文件。

首次编译前必须在 CubeMX 完成以下配置：

1. 在 Connectivity 中关闭 CAN1，并启用 USB Device FS；PA11 为 USB_DM，PA12 为 USB_DP。
2. 为保持最小工程，关闭当前副本不再使用的 USART1、I2C1、I2C2、SPI1、TIM3、PB0 EXTI及相关DMA/NVIC；保留RCC、SYS、GPIO、IWDG、FreeRTOS和USB。
3. 在 Middleware and Software Packs 中启用 `USB_DEVICE`，Class For FS IP 选择 `Communication Device Class (Virtual Port Com)`/CDC。
4. USB时钟必须精确为48 MHz；当前8 MHz HSE、72 MHz PLL配置应将USB分频设为PLLCLK/1.5。
5. 启用共享的 `USB_LP_CAN1_RX0_IRQn`，抢占优先级5、子优先级0，使CDC接收回调可调用FreeRTOS `FromISR` API。
6. 保留FreeRTOS动态弱任务 `appStartTask`、至少8192字节heap；保留IWDG Prescaler 64/Reload 625；PC13保持推挽输出且初始High。
7. 保证 `MX_USB_DEVICE_Init()` 在 FreeRTOS 调度器和 `appStartTask` 之前执行。
8. 确认板上 D+ 有 1.5 kΩ 上拉到 3.3 V；没有时需外接。

CubeMX中的VCP（Virtual COM Port）就是USB CDC ACM串口功能，不需要再寻找另一个独立的“CDC”选项。当前副本已经生成 `USB_DEVICE.CLASS_NAME_FS=CDC`、CDC middleware、描述符、接口文件和 `MX_USB_DEVICE_Init()` 定义。CubeMX把默认调用放在会被SDK强定义覆盖的弱 `appStartTask()` 中，因此USB组件在 `ARK_USB_DEVICE_CUBEMX_INIT` 宏开启时，于自身init回调开头调用 `MX_USB_DEVICE_Init()`；中央`hal_catalog.json`中的STM32F1 `usb_device`驱动负责加入该宏。调用发生在USB stream、CDC接口回调和CLI初始化之前，不修改Cube生成文件，其它平台未定义该宏时不会包含 `usb_device.h` 或引用CubeMX入口。部分CubeMX版本只在 `usb_device.c` 定义 `hUsbDeviceFS` 而不在头文件声明，系统配置生成器会在同一宏下生成类型正确的extern声明以完成句柄绑定。当前审计已通过15/15。

当前描述符使用VID `0x0483`、PID `0x5740`、美国英语语言ID `0x0409`，产品名为 `STM32 Virtual ComPort`。CDC Full Speed数据端点包长64字节，命令端点包长8字节；Cube生成的应用收发缓冲区各1024字节。`USBD_MAX_NUM_INTERFACES=1` 和 `USBD_MAX_NUM_CONFIGURATION=1` 表示一个设备类实例和一个USB配置，不要因为CDC内部含控制/数据两个接口而把前者改成2。调试级别保持0。当前 `USBD_SELF_POWERED=1`；若开发板完全由USB VBUS供电，应在CubeMX改为Bus Powered/0，只有板卡由独立电源供电时才保留Self Powered。

## 14. 维护规则

任何驱动对象、UART 子对象、组件、DTS/manifest、App、任务、脚本或工作流发生变化时，必须同步更新：

1. `skills/ark-embedded-sdk/SKILL.md`
2. `doc/ark_sdk_guide.md`
3. 脚本发生变化时同步更新 `doc/studio_tooling.md`

修改 skill 后运行 skill-creator 的 `quick_validate.py`；修改 C 代码后执行 Keil 全量编译，并要求 0 error、0 warning。

## 15. MicroCar Soil 项目

`app/c8t6_microcar_soil`与`ark_stm32_projects/c8t6_microcar_soil`严格同名；DTS、IOC、Keil工程和Target也全部为`c8t6_microcar_soil`。该项目关闭USB、IWDG、PC13 LED和UART CLI；USART1用于蓝牙，USART2用于MaixCam，USART3用于北斗。

项目级引脚、总线、组件实例与软件策略全部位于`app/c8t6_microcar_soil/c8t6_microcar_soil.dts`：

```text
DHT11              PB5
MQ7 / MQ135         PA4 ADC1_IN4 / PA5 ADC1_IN5
土壤湿度             PB1 ADC1_IN9
OLED + BH1750       I2C1 Remap PB8/PB9, 400 kHz, blocking
蓝牙                USART1 PA9/PA10, 9600, DMA-to-idle RX
北斗                USART3 PB10/PB11, 9600, DMA-to-idle RX
MaixCam             USART2 PA2/PA3, 115200, DMA-to-idle RX
蜂鸣器              PA12, active high
WS2812B             PA7, HAL direct-BSRR software timing, 8 pixels, no persistent framebuffer
TB6612 PWM          PA8 TIM1_CH1 / PA11 TIM1_CH4, App-selected frequency
TB6612 direction    PB12/PB13/PB14/PB15
左/右编码器          TIM2 PA0/PA1 / TIM4 PB6/PB7
舵机                hardware_pwm 绑定PWM，或 software_pwm 使用PA6（microcar_soil）
```

公共 CLI 使用 `ark_cli_command_t` 静态链表。`uart_cli` 只负责终端、历史和行编辑；`help` 遍历注册表，各组件在自身 `<name>_register()` 中注册命令。Watchdog、LED、OLED、ICM20602 等命令不再硬编码在 `uart_cli.c`，因此未选择组件时命令自然不存在。

### microcar_soil SRAM baseline

The current `microcar_soil` OLED configuration adds `dirty_refresh`. It defaults to `true`, preserving the presented-frame cache and page-diff refresh. This App sets it to `false`: `oled_present()` sends eight separately positioned blocking page transfers, while `oled_copy_frame()` still reads the protected working framebuffer. The `oled_fb` CLI command and its 1024-byte diagnostic snapshot buffer are also omitted in this mode. This saves SRAM at the cost of more I2C traffic per presentation; do not use the single 1024-byte horizontal-address transfer.

The current CubeMX FreeRTOS heap is8192 bytes. After reserving the final1-KiB Flash page, the calibration build uses Flash63432/64512 and SRAM18944/20480, leaving1080 and1536 bytes. `businessTask` increased from128 to192 words: the former512-byte stack had a488-byte known calibration call chain plus unknown function-pointer cost and overflowed immediately after a successful white response. Static buffers reduce calibration depth to136 bytes; the rebuilt whole-task maximum is456 bytes against768 allocated bytes.

`microcar_soil` 的 CubeMX `appStart` 任务栈现为 256 words。此前 128 words 在组件初始化/自检和任务自删路径上可能溢出并破坏 FreeRTOS heap，OpenOCD 实测 CPU 停在 `vPortFree()` 的重复释放保护循环。`.ioc` 的任务配置和生成的 `freertos.c` 必须保持同步；重新生成 CubeMX 工程后要再次确认该值。

For encoder bring-up, the OLED bottom row temporarily displays `P:<PA0><PA1> <PB6><PB7>`. These are the live TIM2 CH1/CH2 and TIM4 CH1/CH2 input levels. No bit changes while a wheel turns indicates an encoder power, common-ground, wiring, or signal-level problem. Changing bits with fixed counters indicates a timer mode/start/filter problem and should be followed by TIM2/TIM4 register inspection. Remove this diagnostic after the board is validated.

During this bring-up, the `encoder` CLI also reports raw TIM2/TIM4 counters as `raw=left/right`. Compare them with the software cumulative values: changing raw values with fixed software values indicates an App polling/state issue; fixed raw values indicate the electrical or timer input path.

Encoder polarity is configured by `encoder` properties. TB6612 polarity is configured by the `motor_tb6612` child. The verified board settings remain motor left/right `+1/+1` and encoder left/right `-1/+1`.

### Control component

The `control` component owns reusable integer-scaled PID algorithms and the motor speed-loop state. `control_pid_config_t` supports positional and incremental modes, bounded integral state, and bounded output. The current speed loop uses a 20 ms period and exposes `control_set_speed(left_rpm10, right_rpm10)`, `control_get_speed()`, and `control_update()`; the App schedules it while retaining OLED rendering. The optional smoothing accelerator is independent from PID and uses a squared remaining-distance curve, with separate enable, rise-step, fall-step, and minimum-step parameters. This leaves a stable place for later cascade, parallel, position, and incremental loops.

The `microcar_soil` CLI command is `speed <left_rpm10> <right_rpm10>`. A hardware run at target `700/700` for about 3 seconds produced encoder totals `2411/2469`; `speed 0 0` then stopped both wheels. OLED now shows signed integer RPM without decimal places or cumulative counts.

TB6612 bring-up logs both PWM channel `is_ready/start` results and each configured period. The `motor` CLI reports initialization state and period on failure; direction GPIO activity alone does not prove that TIM1 PWM is running.

STM32F1 HAL defines `TIM_CHANNEL_1` as `0x00000000`; PWM adapter channel validation must therefore explicitly compare against `TIM_CHANNEL_1` through `TIM_CHANNEL_4`. A generic `channel != 0` check rejects CH1 and breaks TB6612 left PWM while CH4 still works.

原 `common_dev` `/dev` 字符串注册表已经删除。工程中没有任何消费者调用其 `open/read/write/ioctl` 分发接口，生产App始终直接使用 `dht11_read()`、`adc_sensor_read()`、`bh1750_read()`、`ws2812b_fill()`、`servo_set_angle()` 等强类型组件接口；关闭CLI后，已注册链表甚至没有遍历入口。继续保留会额外占用节点对象、操作表、包装函数和路径字符串，并把组件初始化结果与无关的节点注册绑定。删除后全量构建实际回收1880字节Flash和248字节SRAM。静态选择的MCU设备统一使用强类型接口；只有存在真实通用消费者时才建立类似 `ark_stream` 的能力专用抽象。传感器显示、低照度点灯、花盆识别和报警时序仍由App协调。

当前App分离`controlTask`、`sensorTask`、`oledTask`和`businessTask`。传感器任务每200ms采集DHT11、MQ7、MQ135、土壤和BH1750，将结果发布到临界区保护的持久快照。土壤探针在复位角时用前8次采样建立空载基线，之后以1/8新值缓慢跟踪；阈值为`baseline_raw-200`，只有低于阈值才视为插入并允许显示或上传。插土流程的有效结果优先。ADC38由20ms业务任务独占采集。传感器任务还按光照档位更新WS2812B，并每1秒通过蓝牙上报传感器、北斗和视觉数据。OLED每100ms绘制，每2秒轮换两页中文传感器数据；每个花盆流程完成后在1秒内闪烁中文“完成”三次。

Microcar Soil必须在`control_register()`前调用`encoder_register()`。CubeMX的TIM2/TIM4 Encoder Mode初始化只配置外设，不会启动计数；编码器组件init负责调用`HAL_TIM_Encoder_Start()`。漏注册时电机仍会在速度PID零反馈下转动，但左右累计计数、实际速度和OLED距离始终为0。

正式循迹将`controlTask`和`businessTask`统一为20ms周期和`idle+2`优先级。业务任务每周期采集ADC38，只用中间6路按`{-1024,-614,-205,205,614,1024}`计算限制在正负1024的位置，以Kp=0.100、Ki=0、Kd=0的位置PID生成正负300 rpm10以内的差速修正并叠加到1200 rpm10基础轮速；控制任务继续执行左右轮速度PID。最外0/7路不参与PID，只生成左`-2048`、右`+2048`、同时触发`0`的直角事件值。

通道0和7作为左右直角专用探头。候选连续确认3次后以1200 rpm10同速前移1700编码器单位，随后完整停车300ms，让车身稳定并消除滑行；稳定结束时重新记录左右编码器作为转弯起点，再以正负800 rpm10原地转向。每次转弯完成后增加全局直角计数，第一个转弯方向保存在`g_app_course_direction`。

转弯后的原3秒停留状态现在运行可复用的`app_flower_tracking`控制器。后置摄像头使用原始480x320坐标，以`(240,92)`为目标点；X误差控制差速转向，Y误差控制前后移动。目标连续识别3帧后才接管底盘，跟踪中允许连续3帧无识别；持续丢失后停车300ms再低速原地扫描。X/Y进入和保持窗口均为正负3像素，停车并连续保持5个新鲜帧后完成。单轮搜索保护时间8秒，App最多执行3轮并交替搜索方向。轮速上限700 rpm10，该低速定位不使用积分项。`APP_FLOWER_LINEAR_POLARITY`、`APP_FLOWER_STEER_POLARITY`和`APP_FLOWER_SEARCH_POLARITY`用于实车方向修正。

舵机通过强类型`servo_set_angle(int16_t)`接口控制，有效范围为`0..180°`，复位角为`SERVO_RESET_ANGLE=0`度。实测负角度不响应，因此脉宽不再向500us以下外推；`0°`对应500us，`180°`对应2500us。

硬件PWM分支仍由定时器持续输出；软件PWM分支不创建常驻任务。组件记录上次成功命令角度，按实测保守速度`45°/200ms`计算普通命令持续时间：`ceil(abs(target-last)*200/45)`毫秒，再向上取整到完整20ms帧。当前0到65度为15帧约300ms，`servo_reset()`始终向0度输出50帧约1秒并重置角度基准。软件帧只在500至2500us高脉宽期间调用`vTaskSuspendAll()`，拉低PA6后立即`xTaskResumeAll()`，低电平阶段用`vTaskDelayUntil()`让OLED、控制和UART任务正常运行。旧实现整段暂停1秒会累计约1000个pending tick，并在舵机抬起结束点集中补算，是系统停止更新的高风险路径。

`businessTask`在`app_line_follow_step()`返回后检查本轮是否已超过20ms周期；舵机等长操作导致超期时，将绝对延时基准重置为当前tick，再等待下一周期，不补跑约50个历史业务周期。土壤状态机也在舵机50帧复位真正结束后更新`state_tick`。

App层`app_soil_probe`执行插土时序：当前`0° -> 65°`、稳定300ms、保持2秒、重试读取土壤ADC最多300ms。ADC读取成功且低于动态插入阈值后才保持探针位置蜂鸣报警3秒，再固定1秒复位回到0度并稳定300ms。基线未建立时拒绝启动；流程已经启动后若采样始终无效，则不鸣叫、保持`sample_valid=false`，复位舵机并进入COMPLETE，让上层继续视觉返航。只有舵机复位失败进入ERROR/FAULT。

OpenOCD实测旧故障现场的CPU和任务仍在运行，业务状态为FAULT而非死锁。土壤基线为4095、动态阈值3895、插入采样仍为4095，原状态机因此在300ms后主动FAULT并调用`servo_reset()`；这解释了“舵机抬起后OLED恢复但小车不返航”。

正式流程在花盆对准时保存连续确认后的首次花盆中心坐标。插土完成后重新启动`app_flower_tracking`，以该首次坐标为动态目标，使后置摄像头回到进入花盆阶段时的相对位置；视觉返航不使用红外控制，最长8秒。编码器左右去程和返程位移只保留为诊断值，不作为返航目标：搜索和转向可能让净位移相互抵消，旧编码器返航因此可能在舵机抬起后直接判定完成而不驱动车轮。返航完成后OLED闪烁中文“完成”三次，共1秒；前3个直角恢复中间6路循迹，并等待最外0/7路连续5个20ms周期释放后才重新布防直角检测。第4个直角完成后进入STOPPED。

MaixCam花盆功能位于`camera/MaixCamPro/flower_pots`，生产入口为`maixpy/main.py`，OpenCV副本为`opencv/main.py`。生产版使用屏幕侧边`/dev/ttyS0`；STM32 USART2 PA2/PA3以115200、DMA-to-idle RX和轮询TX双向通信。协议为`5A 5B len command payload A5`，命令6用于从相机启动业务状态机，详见`flower_pots/protocol.md`。STM32只在STOPPED或FAULT接受命令6，立即响应后保持停车1秒再开始循迹。STOPPED/FAULT期间每1秒持续广播命令7；相机锁存任务就绪状态直到命令6成功，在正常识别页右上角FPS下方显示重试按钮，并在SAVE曾关闭触摸时重新创建触摸线程。

MaixPy启动前10秒使用黑底黄框和大号七段数字显示相机稳定倒计时，并保留“进入循迹校准”按钮。稳定结束后留在启动页，显示“启动任务”按钮并等待用户操作；按键黄色反馈约250ms、发送命令6后才进入花盆识别。校准模式使用独立480x320纯黑画布，分开显示当前Flash/默认参数和本轮采集参数；未采集项不显示数字。五个24像素中文按钮包含白/黑采集、写入、读取和退出，按下后黄色反馈约250ms。白黑两项齐全前写入按钮置灰且协议层拒绝写入，退出校准返回启动页而不自动启动。触摸取LAB阈值、白圈动画、SAVE持久化和保存后停止触摸线程保留；命令7可按需恢复触摸以支持重试。

`camera/MaixCamPro/flower_pots/opencv/main.py`是旧OpenCV对照版，本次不更新其协议和校准UI；生产验证必须使用`maixpy/main.py`。

最终选择 480x320 作为摄像头采集和显示分辨率。程序启动打印 `MAIX_VIDEO`，列出采集、传感器和显示尺寸；`FIT_CONTAIN` 下输入与显示宽高比不同时会留黑边。官方 Camera API 接受偶数宽高而不是固定枚举，常用视觉尺寸包括 320x240、480x320、480x360、640x480，高清档包括 1280x720 和 2560x1440。STM32 解析器使用动态坐标，直接接收当前检测画面的原始几何。

ADC38循迹组件使用PB0/ADC1_IN8模拟输入，通过PC13/PB3/PB4选择8路74HC138/38译码器通道。旧标定值呈明显的0/1、2/3、6/7成对现象，符合A0未切换，因此A0从BOOT1复用的PB2迁移到PC13。PC13只驱动高阻译码输入，低驱动能力足够；PB3/PB4在关闭JTAG并保留SWD后作为GPIO。更换引脚后必须重新执行`track_cal white`和`track_cal black`，旧标定数组不再有效。
ADC38归一化同时支持白底高电压或白底低电压两种极性，并始终将白底映射为4095、黑线映射为0；当前实物属于白底低、黑线高的反向极性。
OLED只链接本项目需要的18个16x16中文字形，数值使用8x16字体。环境页的温度、湿度、光照数值统一从x48开始，单位分别为℃、%和lx；传感器页的一氧化碳、空气质量、土壤湿度数值统一从x80开始并使用%。三个ADC百分比按`round(raw*100/4095)`显示原始满量程占比，不代表校准浓度；土壤未低于动态插入阈值时显示`--`。底行只在视觉阶段显示相机X/Y，其他阶段保持空白，不显示红外循迹数据。
ADC38组件仍提供8路原始和归一化数据，但正式循迹位置由App只使用中间1至6通道，以`{-1024,-614,-205,205,614,1024}`对黑线模拟强度求加权平均并限制在`-1024..+1024`。通道0和7只用于直角事件，不进入PID。生产固件已移除UART CLI，ADC38校准通过MaixCam命令2至5完成。

BH1750按0..7档控制8颗WS2812B白光，将`0..1500 lx`均分为8个约187.5 lx区间，环境越暗档位越高。对应关系为`0..187->7`、`188..374->6`、`375..562->5`、`563..749->4`、`750..937->3`、`938..1124->2`、`1125..1312->1`、`>=1313->0`；RGB依次为`0,12,25,37,50,62,75,88`。只有量化档位变化才执行一次约240us的BSRR+DWT软件发送。组件不做额外校准，高分辨率连续模式按`lux=raw/1.2`换算；内部十分之一lux字段使用`raw*100/12`。

对照`glasses_plus`已验证模块，北斗输入协议为NMEA RMC；当前板载模块实测波特率为9600 8N1，解析器兼容RMC并额外支持GGA和校验和验证。USART3误配为115200时会持续产生帧错误且DMA无法得到有效语句。蓝牙为9600 8N1，固定使用Bluetooth小端模式，不实现4G大端分支。蓝牙builder提供uint8、short、int和float添加接口，发送时固定按该顺序分组输出，各组数量不进入线协议。`microcar_soil`每秒只发送一个36字节帧：包头、已完成直角数uint8、温度/湿度/MQ7/MQ135/土壤/光照/GPS纬度/GPS经度8个float32-le、payload累加和、包尾。当前short/int组为空，不发送type、相机坐标或业务状态；无效float为`0.0f`。接收帧仍为`A5 + data[5] + 累加和 + 5A`共8字节。DTS记录UART期望波特率，实际波特率仍由CubeMX `.ioc`决定，协议实现位于`component/beidou`、`component/bluetooth`和`component/maixcam`。

生产DTS不建立UART Stream或UART CLI启用节点，因此生成`ARK_DTS_HAS_UART_CLI=0`，Keil同步后不包含CLI任务。集中组件/HAL目录与工具链整理后的最新正式全量构建为74/74个编译单元、0错误0警告：Flash使用63488/64512字节、剩余1024字节（98.41%），SRAM使用18480/20480字节、剩余2000字节（90.23%）。Flash余量仍非常紧张，新增功能前必须查看Map并优先裁剪不需要的运行时属性或组件。business额外栈预算仍从固定8192字节FreeRTOS heap分配，不增加链接器统计的ZI；链接期SRAM也不等于任务栈运行峰值，实机优化仍需高水位数据。
ADC1虽然在CubeMX中登记了4个输入通道，但SDK的动态读取接口每次只读取一个通道；STM32F1适配器会在每次读取前强制关闭扫描、清零转换长度并使用Rank1。ADC38需要在每个通道之间切换PC13/PB3/PB4，因此此处不采用DMA批量采样。
本次烧录验证：`mq7` 两次读取为 `2096/2056`，`mq135` 为 `1182/1127`，土壤为 `4095`；ADC38白底校准读取 `77 74 84 93 92 90 91 94`，黑底读取 `610 646 1006 991 309 393 210 232`。四类数据未再出现ADC通道串扰。

第一次编译前必须在CubeMX中关闭USB/USB_DEVICE/IWDG，并生成ADC1、I2C1、USART1/2/3、DMA、TIM1/2/4和GPIO。`microcar_soil`的ADC1还需生成ADC_IN8/PB0；舵机软件PWM使用PA6。ADC38译码地址使用PC13/PB3/PB4，其中PC13推挽输出初始为低。当前工程使用TIM3作为HAL Timebase。STM32F1 ADC适配器首次读取时执行校准，之后使用71.5周期采样时间。

```powershell
```

## 16. Rust Studio 工具服务

工具提供明确参数、取消、真实日志、资源锁和后置校验。编译确认零错误与 AXF；烧录需要确认下载校验及运行结果。清理检查目录边界，创建失败回滚，打包先完整构建再发布。

组合仅在本机保存。选择 App 不自动同步工程，选择 SDK 不更换 App，工程变更使 Target 回到待选择。本轮不新增任务历史或产物浏览独立页面。

## ESP-01 TCP 网络 App

以下设备侧协议及网页流程包含历史联调记录。当前 ark_web 仓库为空，网页功能不能视为当前交付。

c8t6_ark_net 的 CLI 是 USART1/921600：wifi diag、wifi at、net status、net diag 可用于诊断。正常状态为 Wi-Fi state=2、UART errors=0、net session=1 和 tcp=1。

ESP 复位时 ROM 可能以另一波特率输出启动文本；组件延后 3 秒开始接收。AT 返回 busy 表示上一个操作尚未完成，会自动退避。CWJAP 中出现 WIFI DISCONNECT 后仍可能继续 CONNECTED/GOT IP/OK，因此等待最终 OK。网页新增设备时填写 DTS 中的设备 ID 和 ark,device-password；设备每 5 秒心跳，30 秒无心跳时网页禁用 LED 开关。

网页访问前必须登录 `admin`。生产部署应在该服务的 `.env` 中设置 `ADMIN_PASSWORD`。网页用户密码与设备 DTS 密码用途不同，前者只保护管理控制台。

当前 Web 控制台包含设备总览、设备详情、遥测趋势与运行记录。SQLite 独立维护 `users`、`sessions`、`devices`、`telemetry`、`activity` 五张表；会话和设备密码均不再以可直接使用的明文保存。服务启动会兼容迁移旧数据库中的固定盐用户哈希和明文设备密码，因此部署升级时必须保留 `data` 卷。设备详情默认读取最近 90 条遥测用于趋势图，每台设备最多保留最近 2880 条历史。

管理 API 的统一入口为 `/api/v1/overview`。前端每 5 秒刷新当前页面，请求超时为 8 秒；设备离线、会话过期、表单校验失败和服务器错误都会提供可恢复提示。静态资源明确使用 UTF-8 和 `no-store` 缓存策略，修改前端后不需要依赖手动清除浏览器缓存。
