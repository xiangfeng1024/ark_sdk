# c8t6_ark_net 项目需求

这是 ESP-01、HTTP/JSON 与 ST7789 LCD 的独立联调 App。App、DTS、CubeMX目录、IOC、
Keil工程和 Target 均为 `c8t6_ark_net`。

## 当前组件

- LCD：ST7789，SPI1 + DMA，PA5/PA7，控制脚 BL/CS/DC/RST 为 PA1/PA2/PA3/PA4。
- CLI：USART1，沿用模板的 DMA-to-idle 配置。
- ESP-01：USART3 PB10/PB11，115200，RX 中断模式。
- LED：PC13，有效低电平；网页命令通过 HTTP 拉取后控制该 GPIO。
- JSON：最小 cJSON 移植；仅包含 `cJSON.c/.h`，不包含 Utils 或测试代码。

## 必须由用户在 CubeMX 完成的配置

1. 启用 USART3 Async，PB10=TX、PB11=RX、115200、8N1、无校验。
2. 在 NVIC 启用 USART3 global interrupt。
3. USART3 RX **不启用 DMA**。SPI1_TX DMA 使用 DMA1 Channel3，与 USART3_RX DMA 冲突。
4. 如需要提高发送吞吐，可只启用 USART3_TX DMA（DMA1 Channel2）；当前组件默认使用阻塞发送。
5. 保留 SPI1 18 MHz、Mode 0、TX DMA，保留 USART1 CLI 配置；PC13 配置为 GPIO 输出。
6. 生成代码后执行 DTS 生成和同步 Keil。不要手改 CubeMX 生成的 C/H。

## 网络配置

在 `c8t6_ark_net.dts` 修改：

- `wifi_esp_at` 的 `ark,ssid`、`ark,password`、stream 与可选 UDP/TLS 能力。
- `ark_net` 的 `ark,device-id`、`ark,device-password`、主机和 TCP 端口。

网页服务器为 `http://154.202.119.145:8080`，设备 TCP 网关为
`154.202.119.145:9000`。设备先发送 `hello`，再发送 `telemetry` 和
5 秒 `heartbeat`；服务端 30 秒未收到数据即离线，每次响应返回 LED 状态和修订号。

DHT11 已接在 PB5，App 只上传校验成功的真实温湿度。可用 `net status` 与
`net diag` 检查 TCP 会话、状态回包和错误码。
