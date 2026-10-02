# wifi_esp_at 组件用户开发指南

## 1. 组件定位

ESP AT 固件后端，可用于 ESP8266/ESP-01 或运行兼容 AT 固件的 ESP32。它负责 AT 探测、AP 连接和 socket 命令翻译，不负责 HTTP/MQTT/JSON 等协议组包。

## 2. DTS 配置

```dts
wifi_esp_at {
    compatible = "wifi_esp_at";
    ark,stream-name = "esp01";
    current-speed = <115200>;
    ark,ssid = "YOUR_SSID";
    ark,password = "YOUR_PASSWORD";
    ark,udp-enabled;
    status = "okay";
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `ark,stream-name` | 是 | 字符串 | 独占使用的 stream。 |
| `current-speed` | 是 | bit/s | AT 串口波特率。 |
| `ark,ssid` / `ark,password` | 是 | 字符串 | AP 凭据。 |
| `ark,udp-enabled` | 否 | 布尔 | 声明固件支持 UDP CIPSTART。 |
| `ark,tls-enabled` | 否 | 布尔 | 声明固件支持 SSL CIPSTART。 |
| 父节点 | 是 | `wifi` | 后端不能独立存在。 |

```powershell
python -m studio.cli dts app/<app> --check
python -m studio.cli dts app/<app>
```

## 3. API 参考

| API | 作用 |
|---|---|
| `wifi_esp_at_register()` | 注册 ESP AT 后端。 |
| `wifi_esp_at_probe()` | 发送 `AT` 并返回原始响应，供诊断使用。 |

业务连接和收发统一调用 `wifi_*` API。

## 4. 使用流程

在 stream 下建立 wifi 父节点和本后端；初始化任务等待模块启动，创建接收任务并附加到 wifi 核心。上层按 capabilities 选择 socket 类型。

## 5. 注意事项

- 不要把 SSID/密码写入日志；后端当前使用单连接 `CIPMUX=0`。
- UDP/TLS 能力必须与实际 AT 固件一致，声明能力不等于完成证书验证。
- RX 任务是 stream 的唯一读取者，协议层只能通过 wifi socket 接口收包。

## 6. 验证

覆盖 AT、错误凭据、模块复位、TCP/UDP、固件不支持 SSL、接收溢出和重连。
