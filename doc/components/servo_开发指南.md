# servo 组件用户开发指南

## 1. 组件定位

0 至 180 度舵机控制组件，支持硬件 PWM 或 GPIO 软件 PWM；依赖 `pwm`，软件模式另依赖 `gpio`、`time`。

## 2. DTS 配置

软件 PWM：

```dts
servo {
    compatible = "servo";
    ark,software-pwm;
    data-gpios = <&gpioa 6 0>;
    status = "okay";
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `compatible` | 是 | 字符串 | 固定为 `"servo"`。 |
| `ark,software-pwm` | 条件 | 布尔属性 | 选择 GPIO 软件 PWM。 |
| `data-gpios` | 软件模式 | GPIO phandle | 软件 PWM 输出脚。 |
| `pwms` | 硬件模式 | PWM phandle | 硬件 PWM provider。 |
| `status` | 否 | 字符串 | 控制组件是否启用。 |

硬件 PWM 使用 `pwms = <&pwm1>;` 并移除 `ark,software-pwm`。两种 transport 只能选择一种。

配置完成后执行 DTS 校验和生成：

```powershell
../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe dts.generate --input doc/examples/dts-参数.json
```

## 3. API 参考

| API | 作用 |
|---|---|
| `servo_register()` | 加载 transport 并注册组件。 |
| `servo_set_angle(angle)` | 设置 0..180 度目标角。 |
| `servo_set_pulse_us(pulse_us)` | 直接设置微秒脉宽。 |
| `servo_reset()` | 回到 `SERVO_RESET_ANGLE`。 |
| `servo_disable()` | 停止输出控制脉冲。 |

## 4. 使用流程

选择 PWM transport 并生成 DTS；初始化后先 reset，再逐步设置角度，动作结束需要卸力时 disable。

## 5. 注意事项

- 舵机电源通常不能直接由 MCU 供电，必须共地并满足峰值电流。
- 超出机械行程的脉宽可能堵转，应按实际舵机限制范围。
- 软件 PWM 占用时序资源且更易受中断抖动影响，优先硬件 PWM。

## 6. 验证

断开负载测量 20 ms 周期和脉宽，再测试 0/90/180 度；确认 disable 后波形停止且机械无碰撞。
