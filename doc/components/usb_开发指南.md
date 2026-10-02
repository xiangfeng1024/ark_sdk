# usb 组件用户开发指南

## 1. 组件定位

CubeMX USB CDC 到 `ark_stream` 的适配组件，为 CLI 或协议提供 USB 虚拟串口；依赖 `usb_device` HAL/中间件。

## 2. DTS 配置

```dts
usb {
    compatible = "usb";
    ark,stream-name = "usb_cli";
    status = "okay";
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `compatible` | 是 | 字符串 | 固定为 `"usb"`。 |
| `ark,stream-name` | 是 | 字符串 | 注册到 `ark_stream` 的唯一名称。 |
| `status` | 否 | 字符串 | 控制组件是否启用。 |

工程必须包含 CubeMX USB Device/CDC 初始化与回调桥接。

配置完成后执行 DTS 校验和生成：

```powershell
../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe dts.generate --input doc/examples/dts-参数.json
```

## 3. API 参考

| API | 作用 |
|---|---|
| `usb_config_get()` | 返回只读 stream 名称配置。 |
| `usb_register()` | 注册 USB CDC 组件，初始化接收缓冲、发送互斥和 stream。 |
| `ark_stream_find("usb_cli")` | 取得 USB 字节流，后续使用公共 stream API。 |

## 4. 使用流程

完成 CubeMX USB CDC 工程配置，启用组件；枚举完成后由 CLI/协议按 stream 名称连接，主机断开时等待并重试。

## 5. 注意事项

- USB 枚举前 stream 可能未就绪，不得永久阻塞整个组件启动。
- 不直接修改生成的 `usb_device.c`/`usbd_cdc_if.c` 核心区；桥接放在允许的用户区域或 SDK 适配层。
- 发送缓冲和 CDC busy 状态必须处理，禁止在 ISR 中阻塞写。

## 6. 验证

在 Windows 枚举虚拟串口，执行收发回环、拔插、主机未打开、连续大包和 busy 重试测试。
