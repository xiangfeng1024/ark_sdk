# motor 组件用户开发指南

## 1. 组件定位

`motor` 是统一电机执行器核心，不直接包含 GPIO、PWM 或 CAN 细节。实际输出由同目录的 `motor_tb6612` 或 `motor_can` 后端提供；一个 motor 实例只允许启用一个后端。

## 2. DTS 配置

```dts
motor {
    compatible = "motor";
    status = "okay";
    motor_tb6612 { compatible = "motor_tb6612"; /* resources */ };
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `compatible` | 是 | 字符串 | 固定为 `"motor"`。 |
| 后端子节点 | 是 | `motor_tb6612` 或 `motor_can` | 必须且只能启用一个。 |
| `status` | 否 | 字符串 | 控制整个电机类。 |

```powershell
python -m studio.cli dts app/<app> --check
python -m studio.cli dts app/<app>
```

## 3. API 参考

| API | 作用 |
|---|---|
| `motor_register()` | 注册电机核心。 |
| `motor_is_ready()` | 检查后端是否已附加并可用。 |
| `motor_backend_name()` | 返回当前后端名称。 |
| `motor_set_duty_permille()` | 对支持占空比的后端设置左右通道 -1000..1000。 |
| `motor_set_currents()` | 对 CAN 电流型后端设置四路 16 位电流。 |
| `motor_stop()` | 调用当前后端的安全停止。 |

## 4. 使用流程

在 motor 子节点选择后端；后端 Level 2 初始化并附加操作表，motor Level 3 验证后提供统一 API。上层控制算法不得包含后端判断。

## 5. 注意事项

- 不支持的能力返回 `false`，例如 TB6612 不支持四路 CAN 电流。
- 同时启用两个 motor backend 会被 DTS 和运行时拒绝。
- 故障、模式切换和退出任务必须调用 `motor_stop()`。

## 6. 验证

分别构建 TB6612 和 CAN DTS，确认只纳入对应 HAL；测试未知/双后端、越界占空比和停止路径。
