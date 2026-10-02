# stream 组件用户开发指南

## 1. 组件定位

UART 到 `ark_stream` 的传输适配组件，扫描多个 DTS stream 节点并提供与 UART/DMA/IRQ 无关的 `find/read/write` 字节流服务；依赖 `uart` HAL。

## 2. DTS 配置

```dts
uart3: uart3 {
    compatible = "ark_hal_uart";
    stream {
        compatible = "stream";
        ark,stream-name = "esp01";
        current-speed = <115200>;
        ark,rx-mode = "interrupt";
        status = "okay";
    };
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `compatible` | 是 | 字符串 | 固定为 `"stream"`。 |
| `ark,stream-name` | 是 | 字符串 | 全局唯一逻辑名称。 |
| `current-speed` | 是 | bit/s | UART 波特率。 |
| `ark,rx-mode` | 否 | 枚举字符串 | `interrupt`、`dma` 或 `dma-to-idle`。 |
| `status` | 否 | 字符串 | 控制 stream 是否启用。 |

stream 必须是 UART provider 子节点。

配置完成后执行 DTS 校验和生成：

```powershell
../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe dts.generate --input doc/examples/dts-参数.json
```

## 3. API 参考

| API | 作用 |
|---|---|
| `stream_register()` | 扫描 DTS 中所有 stream 实例并注册 transport。 |
| `ark_stream_find(name)` | 按逻辑名称取得 stream；定义在公共 `ark_stream.h`。 |
| `ark_stream_is_ready/read/write()` | 查询状态或按超时读写字节。 |

## 4. 使用流程

配置 UART provider 和唯一 stream 名称；框架 Level 2 初始化后，协议组件在 init 中 find 并缓存指针，任务上下文通过 read/write 使用。

## 5. 注意事项

- 一个物理 UART 只能注册一个 stream，一个 stream 默认只能有一个消费型读取者。
- RX 模式必须匹配 CubeMX DMA/IRQ 配置；ISR 仅复制和通知。
- `ARK_STREAM_WAIT_FOREVER` 只能用于专用后台任务，不能用于启动、自检或控制周期。

## 6. 验证

分别做 interrupt/DMA 模式回环，测试唯一名称、重复 UART、溢出和超时；确认协议组件不包含 CubeMX UART 句柄。
