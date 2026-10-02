# flash 组件用户开发指南

## 1. 组件定位

`flash` 是统一非易失存储核心，通过设备对象提供容量、擦除块、读、写和擦除。STM32F103 片内 Flash 与 W25Q16 都是同目录可裁剪后端；应用不再直接绑定器件函数。

## 2. DTS 配置

```dts
flash {
    compatible = "flash";
    status = "okay";
    internal_flash { compatible = "flash_stm32f103"; status = "okay"; };
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `compatible` | 是 | 字符串 | 固定为 `"flash"`。 |
| 后端子节点 | 是 | 一个或多个 | `flash_stm32f103`、`flash_w25q16`。 |
| `status` | 否 | 字符串 | 控制整个存储类。 |

```powershell
../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe dts.generate --input doc/examples/dts-参数.json
```

## 3. API 参考

| API | 作用 |
|---|---|
| `flash_register()` / `flash_is_ready()` | 注册或检查存储核心。 |
| `flash_default()` / `flash_find()` | 获取默认设备或按后端名称查找。 |
| `flash_name()` / `flash_capacity()` / `flash_erase_block_size()` | 查询设备元数据。 |
| `flash_read()` / `flash_write()` / `flash_erase()` | 在容量边界内执行标准存储操作。 |

## 4. 使用流程

启用 flash 父节点和后端；启动完成后缓存 `flash_device_t` 指针，再进行带地址和长度的读写。记录格式、版本、CRC、事务与磨损均由上层负责。

## 5. 注意事项

- 设备指针只读且在启动后稳定；写擦操作不能在 ISR 调用。
- 不同后端的擦除粒度和寿命不同，上层必须查询而非硬编码。
- 多后端名称必须唯一。

## 6. 验证

覆盖设备查找、越界、零长度、擦除粒度、写后读回和复位持久化；确认只选择实际后端 HAL。
