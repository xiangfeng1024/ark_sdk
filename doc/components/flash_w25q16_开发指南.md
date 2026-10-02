# flash_w25q16 组件用户开发指南

## 1. 组件定位

`flash` 类的 W25Q16 SPI NOR 后端，容量 2 MiB、扇区 4 KiB，支持跨页读取、写入前按需擦除和 JEDEC ID 诊断。

## 2. DTS 配置

```dts
flash_w25q16@0 {
    compatible = "flash_w25q16";
    reg = <0>;
    cs-gpios = <&gpioa 4 1>;
    status = "okay";
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `cs-gpios` | 是 | GPIO phandle | SPI 片选。 |
| SPI provider 祖先 | 是 | `ark_hal_spi` | 提供实际 SPI ID。 |
| 父节点 | 是 | `flash` | 后端不能独立存在。 |
| `status` | 否 | 字符串 | 控制后端。 |

```powershell
../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe dts.generate --input doc/examples/dts-参数.json
```

## 3. API 参考

| API | 作用 |
|---|---|
| `flash_w25q16_register()` | 注册 W25Q16 后端。 |
| `flash_w25q16_read_id()` | 读取 3 字节 JEDEC ID。 |

普通存储操作使用 `flash_read/write/erase()`。

## 4. 使用流程

在 flash 父节点内配置后端与 SPI/CS，先读取 ID，再按统一设备 API 操作；上层设计分区、记录和 CRC。

## 5. 注意事项

- 自检会使用最后一个 4 KiB 扇区，量产分区必须保留该区域或关闭破坏性自检。
- 写 0 到 1 需要擦除，操作均有超时且不能在 ISR 调用。

## 6. 验证

核对 JEDEC ID，执行跨页写、跨扇区擦除、读回和边界测试，并确认工程仅加入 SPI/GPIO HAL。
