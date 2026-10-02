# ark_net 组件用户开发指南

## 1. 组件定位

ARK 设备业务协议组件。`ark_net_protocol.c` 负责 hello、telemetry、heartbeat 和 command JSONL 的组包/解包；`ark_net.c` 负责会话、重连、周期与业务快照；底层只调用通用 `wifi` socket。

## 2. DTS 配置

```dts
ark_net {
    compatible = "ark_net";
    ark,host = "server.example.com";
    ark,port = <9000>;
    ark,device-id = "device-01";
    ark,device-password = "SECRET";
    ark,upload-period-ms = <5000>;
    status = "okay";
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `ark,host` / `ark,port` | 是 | 主机、TCP 端口 | 设备网关。 |
| `ark,device-id` / `ark,device-password` | 是 | 字符串 | JSONL hello 鉴权。 |
| `ark,upload-period-ms` | 否 | 毫秒 | 遥测周期，最小 5000。 |
| 依赖 | 是 | `wifi`、`json` | DTS 必须同时启用。 |

```powershell
python -m studio.cli dts app/<app> --check
python -m studio.cli dts app/<app>
```

## 3. API 参考

| API | 作用 |
|---|---|
| `ark_net_register()` | 注册业务会话。 |
| `ark_net_set_telemetry()` / `ark_net_get_telemetry()` | 更新或读取温湿度 x10 快照。 |
| `ark_net_get_command()` | 读取远端 LED 命令与 revision。 |
| `ark_net_publish()` / `ark_net_poll()` | 同步执行上报或 heartbeat。 |
| `ark_net_request_publish()` / `ark_net_request_poll()` | 通知后台任务异步执行。 |
| `ark_net_protocol_encode_*()` | 生成完整换行结尾 JSONL 包。 |
| `ark_net_protocol_decode_command()` | 校验并解析 command JSON。 |

## 4. 使用流程

Wi-Fi 核心先连接 AP，ark_net 打开 TCP socket；协议层生成完整包后交给 socket，接收字节交回协议层解析。更换 Wi-Fi 模块不修改业务协议，更换设备协议不修改驱动。

## 5. 注意事项

- MQTT、HTTP/HTTPS 应新增独立 net 协议源码并复用 wifi socket，不能塞进 ESP AT 后端。
- device ID/password 中的控制字符、引号和反斜杠会被协议层拒绝。
- 实时任务使用 request API；密码不得打印或回显。

## 6. 验证

单测 hello/telemetry/heartbeat 精确字节、非法字符串和 command 解析；联调断线重连、重复 revision、超时与日志脱敏。
