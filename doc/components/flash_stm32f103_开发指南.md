# flash_stm32f103 组件用户开发指南

## 1. 组件定位

`flash` 类的 STM32F103 片内后端，提供固定 512 字节逻辑存储区。为控制 RAM 和代码体积，写入与擦除从逻辑地址 0 开始并重写记录，上层应把它作为单记录区使用。

## 2. DTS 配置

```dts
internal_flash {
    compatible = "flash_stm32f103";
    ark,reserve-bytes = <1024>;
    status = "okay";
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `ark,reserve-bytes` | 是 | 字节 | 链接末尾至少保留 1024。 |
| 父节点 | 是 | `flash` | 由统一核心访问。 |
| `status` | 否 | 字符串 | 控制后端。 |

```powershell
../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe dts.generate --input doc/examples/dts-参数.json
```

## 3. API 参考

| API | 作用 |
|---|---|
| `flash_stm32f103_register()` | 注册片内后端；业务读写使用 flash 核心 API。 |

## 4. 使用流程

确认 Keil MAP 未占用保留页；使用 `flash_find("internal")` 获取设备。读取可指定范围，写入与擦除必须从地址 0 开始。

## 5. 注意事项

- 固定逻辑区地址 `0x0800FE00`，物理擦除页起始 `0x0800FC00`，仅适用于当前 F103 64 KiB 布局。
- 每次局部写仍会擦除整页，有寿命和掉电一致性风险。

## 6. 验证

检查 MAP、写入、复位、读回、CRC、局部更新保持和容量越界。
