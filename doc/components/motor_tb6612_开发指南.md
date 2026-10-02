# motor_tb6612 组件用户开发指南

## 1. 组件定位

`motor` 类的 TB6612 双路 PWM 后端，负责两路 PWM、四根方向 GPIO 和方向极性，不向上层暴露 TB6612 专用控制 API。

## 2. DTS 配置

```dts
motor_tb6612 {
    compatible = "motor_tb6612";
    pwms = <&pwm1>, <&pwm2>;
    left-in1-gpios = <&gpiob 13 0>; left-in2-gpios = <&gpiob 12 0>;
    right-in1-gpios = <&gpiob 14 0>; right-in2-gpios = <&gpiob 15 0>;
    ark,left-polarity = <1>; ark,right-polarity = <1>;
    status = "okay";
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `pwms` | 是 | 2 个 PWM phandle | 左、右通道。 |
| 四个 `*-gpios` | 是 | GPIO phandle | 两路方向脚。 |
| `ark,left-polarity` / `right-polarity` | 是 | `1` 或 `-1` | 机械方向修正。 |
| 父节点 | 是 | `motor` | 后端不能独立存在。 |

```powershell
python -m studio.cli dts app/<app> --check
python -m studio.cli dts app/<app>
```

## 3. API 参考

| API | 作用 |
|---|---|
| `motor_tb6612_register()` | 注册并在初始化后附加到 motor 核心。 |

业务使用 `motor_set_duty_permille()` 与 `motor_stop()`。

## 4. 使用流程

配置 motor 父节点和本后端，框架初始化后由 control 或 App 调用统一 motor API。

## 5. 注意事项

- 换向前考虑制动和机械冲击；电源、STBY 和共地必须正确。
- 极性只在 DTS 修正，不在业务代码交换左右或取反。

## 6. 验证

架空左右轮测试正转、反转、零、限幅和故障停车，并确认工程未引入 CAN HAL。
