# wifi 组件用户开发指南

## 1. 组件定位

`wifi` 是无线链路核心，向上提供 AP 连接状态以及 TCP、UDP、TLS socket。它不理解 HTTP、MQTT、设备鉴权或业务包；这些标准与组包属于 `ark_net` 或其它 net 协议源码。

链路为：`ark_net/HTTP/MQTT -> wifi socket -> wifi_esp_at -> ark_stream -> UART`。

## 2. DTS 配置

```dts
wifi {
    compatible = "wifi";
    status = "okay";
    wifi_esp_at { compatible = "wifi_esp_at"; /* credentials */ };
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `compatible` | 是 | 字符串 | 固定为 `"wifi"`。 |
| 后端子节点 | 是 | `wifi_esp_at` | 当前必须且只能启用一个。 |
| `status` | 否 | 字符串 | 控制无线类。 |

```powershell
python -m studio.cli dts app/<app> --check
python -m studio.cli dts app/<app>
```

## 3. API 参考

| API | 作用 |
|---|---|
| `wifi_register()` / `wifi_is_ready()` / `wifi_get_state()` | 注册或查询无线链路。 |
| `wifi_get_capabilities()` | 查询当前后端是否支持 TCP、UDP、TLS。 |
| `wifi_connect()` / `wifi_disconnect()` | 加入或离开 DTS 配置的 AP。 |
| `wifi_socket_open()` | 按 TCP、UDP 或 TLS 打开远端 socket。 |
| `wifi_socket_send()` / `wifi_socket_receive()` | 传输原始字节，不解释上层协议。 |
| `wifi_socket_close()` / `wifi_socket_is_open()` | 关闭或查询 socket。 |

## 4. 使用流程

先查询 capabilities，再连接 AP、打开所需 socket、收发协议层已组好的数据，结束后关闭 socket。MQTT/HTTP/HTTPS 组件应在此 API 上实现包格式与状态机。

## 5. 注意事项

- `WIFI_SOCKET_TLS` 只代表加密字节流，证书、SNI 和固件能力仍需后端支持。
- 同一后端当前只维护一个 socket；并发连接需扩展句柄模型。
- 网络重连和业务重试应分层处理，Wi-Fi 不保存设备业务状态。

## 6. 验证

覆盖能力查询、错误协议、AP 断线、TCP/UDP 回环、TLS 未支持和 socket 超时。
