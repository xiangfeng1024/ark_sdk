# buzzer 组件用户开发指南

## 1. 组件定位

单 GPIO 有源蜂鸣器控制组件，提供持续开关和定时鸣叫；依赖 `gpio` HAL。

## 2. DTS 配置

```dts
buzzer {
    compatible = "buzzer";
    gpios = <&gpioa 12 0>;
    status = "okay";
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `compatible` | 是 | 字符串 | 固定为 `"buzzer"`。 |
| `gpios` | 是 | GPIO phandle | 蜂鸣器控制脚及有效电平。 |
| `status` | 否 | 字符串 | 控制组件是否启用。 |

配置完成后执行 DTS 校验和生成：

```powershell
../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe dts.generate --input doc/examples/dts-参数.json
```

## 3. API 参考

| API | 作用 |
|---|---|
| `buzzer_register()` | 注册组件。 |
| `buzzer_set(enabled)` | 立即打开或关闭蜂鸣器。 |
| `buzzer_beep(duration_ms)` | 打开并阻塞指定毫秒后关闭。 |

## 4. 使用流程

绑定 GPIO 并生成 DTS；初始化后用 `buzzer_set()` 控制持续状态，任务上下文中可用 `buzzer_beep()` 发提示音。

## 5. 注意事项

- `buzzer_beep()` 会等待，禁止在 ISR 或硬实时控制路径调用。
- 无源蜂鸣器需要 PWM，本组件不负责产生音调。
- 上电极性配置错误可能导致持续鸣叫。

## 6. 验证

执行 DTS/pytest 检查；目标板分别测试开、关和短时 beep，并测量 GPIO 有效电平。
