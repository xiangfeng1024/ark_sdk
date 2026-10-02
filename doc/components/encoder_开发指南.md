# encoder 组件用户开发指南

## 1. 组件定位

左右轮正交编码器组件，读取两个 encoder HAL 实例，输出累计计数和增量，并提供 `rpm10` 换算。

## 2. DTS 配置

```dts
encoder {
    compatible = "encoder";
    encoders = <&encoder1>, <&encoder2>;
    ark,left-polarity = <0xffffffff>;
    ark,right-polarity = <1>;
    ark,counts-per-revolution = <1500>;
    status = "okay";
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `compatible` | 是 | 字符串 | 固定为 `"encoder"`。 |
| `encoders` | 是 | 2 个 encoder phandle | 依次为左轮、右轮。 |
| `ark,left-polarity` | 是 | s32 | 左轮方向，`1` 或 `-1`。 |
| `ark,right-polarity` | 是 | s32 | 右轮方向，`1` 或 `-1`。 |
| `ark,counts-per-revolution` | 是 | 计数/圈 | 必须大于 0。 |
| `status` | 否 | 字符串 | 控制组件是否启用。 |

配置完成后执行 DTS 校验和生成：

```powershell
../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe dts.generate --input doc/examples/dts-参数.json
```

## 3. API 参考

| API | 作用 |
|---|---|
| `encoder_register()` | 注册组件和 CLI。 |
| `encoder_reset()` | 清零软件累计与增量基准。 |
| `encoder_get(data)` | 读取左右累计计数和本次增量。 |
| `encoder_delta_to_rpm10(delta, period_ms)` | 按配置线数和周期换算 0.1 RPM。 |

## 4. 使用流程

配置两个 CubeMX encoder provider 后生成 DTS；启动时 reset，控制任务每个固定周期 get，再用同一周期换算速度。

## 5. 注意事项

- 左右 provider 顺序与 polarity 必须匹配机械方向。
- `period_ms` 不能为 0，且必须等于真实采样间隔。
- 需要考虑硬件计数器回绕；不要在 ISR 与任务中并发 reset/get。

## 6. 验证

架空车轮分别正反转，确认左右符号、每圈计数和 rpm10；测试计数器回绕及停止时增量为零。
