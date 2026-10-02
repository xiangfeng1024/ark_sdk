# beidou 组件用户开发指南

## 1. 组件定位

北斗/GNSS NMEA 接收组件，通过父级 `stream` 解析 RMC/GGA 数据并发布经纬度快照，不直接管理 UART。

## 2. DTS 配置

```dts
stream_beidou: stream {
    compatible = "stream";
    ark,stream-name = "beidou";
    current-speed = <9600>;
    beidou { compatible = "beidou"; status = "okay"; };
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| 子节点 `compatible` | 是 | 字符串 | 固定为 `"beidou"`。 |
| 父节点 `ark,stream-name` | 是 | 字符串 | 组件查找并独占读取的 stream 名称。 |
| 父节点 `current-speed` | 是 | bit/s | GNSS 模块串口波特率。 |
| `status` | 否 | 字符串 | 控制协议组件是否启用。 |

物理 UART 和接收模式由父 stream 配置。

配置完成后执行 DTS 校验和生成：

```powershell
../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe dts.generate --input doc/examples/dts-参数.json
```

## 3. API 参考

| API | 作用 |
|---|---|
| `beidou_register()` | 注册协议组件。 |
| `beidou_get_position(position)` | 复制最新定位；经纬度单位为 1e-7 度，`valid` 表示定位有效。 |

## 4. 使用流程

先启用 UART provider 和 stream，再启用 beidou；初始化后后台任务持续解析，应用周期读取最新快照并检查 `valid` 和 `tick`。

## 5. 注意事项

- 只接受校验和正确的 NMEA 行；无定位时不能沿用旧坐标冒充新数据。
- stream 应由本组件独占消费，禁止另一个协议任务同时读取。
- 天线环境和冷启动时间会影响首次有效定位。

## 6. 验证

向 stream 回放带正确/错误 XOR 校验的 RMC/GGA 样本，运行 pytest；目标板确认 9600 波特率和定位 tick 持续更新。
