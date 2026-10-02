# bluetooth 组件用户开发指南

## 1. 组件定位

蓝牙串口业务协议组件，通过 `ark_stream` 接收固定 5 字节控制数据，并提供带头尾与校验的多类型数据包构建器。

## 2. DTS 配置

```dts
stream_bluetooth: stream {
    compatible = "stream";
    ark,stream-name = "bluetooth";
    current-speed = <9600>;
    bluetooth { compatible = "bluetooth"; status = "okay"; };
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| 子节点 `compatible` | 是 | 字符串 | 固定为 `"bluetooth"`。 |
| 父节点 `ark,stream-name` | 是 | 字符串 | 蓝牙协议绑定的 stream。 |
| 父节点 `current-speed` | 是 | bit/s | 与蓝牙模块一致的波特率。 |
| `status` | 否 | 字符串 | 控制协议组件是否启用。 |

协议组件不配置 UART 句柄。

配置完成后执行 DTS 校验和生成：

```powershell
../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe dts.generate --input doc/examples/dts-参数.json
```

## 3. API 参考

| API | 作用 |
|---|---|
| `bluetooth_register()` | 注册协议组件并启动解析生命周期。 |
| `bluetooth_read(data, size)` | 从内部环形缓存读取原始字节，返回字节数或负值。 |
| `bluetooth_packet_reset()` | 清空包构建器。 |
| `bluetooth_packet_add_uint8()` | 追加一个 8 位无符号字段。 |
| `bluetooth_packet_add_short()` | 追加一个 16 位有符号小端字段。 |
| `bluetooth_packet_add_int()` | 追加一个 32 位有符号小端字段。 |
| `bluetooth_packet_add_float()` | 追加一个 float 字段。各类型容量满时均返回 `false`。 |
| `bluetooth_packet_send()` | 编码校验并通过 stream 发送构建结果。 |
| `bluetooth_get_control()` | 获取最新 5 字节控制快照、序列号和 tick。 |

## 4. 使用流程

启用 UART 与 stream，初始化后周期读取 `bluetooth_get_control()`；发送时 reset、按顺序 add、最后 send，并检查每步返回值。

## 5. 注意事项

- 接收帧头为 `0xA5`、帧尾为 `0x5A`，使用累加校验；两端字段顺序必须一致。
- 构建器各类型有固定容量，不能忽略 add 失败。
- stream 只能由一个消费协议读取；解析运行于任务上下文。

## 6. 验证

用串口回环覆盖正确帧、错误校验、错位帧和构建器溢出；目标板确认 sequence 只在有效帧递增。
