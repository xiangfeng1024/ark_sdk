# control 组件用户开发指南

## 1. 组件定位

`control` 是控制核心层，负责策略选择、左右轮目标速度、编码器反馈、速度 PID 和电机输出。`control_pid` 是无硬件依赖的纯算法；`control_line_tracking` 是内置循迹策略。避障、路径规划等逻辑通过 `control_strategy_t` 注册，不应继续堆入 `control.c`。

链路为：`策略 -> control_command_t -> 速度闭环 -> motor -> motor backend`。

## 2. DTS 配置

```dts
control {
    compatible = "control";
    ark,period-ms = <20>;
    ark,left-pid = <0 350 60 0 0xfffffc18 1000 0xfffcf2c0 200000>;
    ark,right-pid = <0 350 60 0 0xfffffc18 1000 0xfffcf2c0 200000>;
    ark,smoothing-enabled;
    ark,rise-step-rpm10 = <180>;
    ark,fall-step-rpm10 = <300>;
    ark,minimum-step-rpm10 = <10>;
    status = "okay";
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `ark,period-ms` | 是 | 毫秒 | 闭环更新周期。 |
| `ark,left-pid` / `ark,right-pid` | 是 | 8 个 s32 | 模式、Kp/Ki/Kd、输出与积分上下限。 |
| `ark,smoothing-enabled` | 否 | 布尔 | 启用目标速度斜坡。 |
| `ark,*-step-rpm10` | 是 | 0.1 RPM/周期 | 上升、下降和最小步长。 |
| `/software/line_follow/ark,steering-pid` | 循迹时 | 8 个 s32 | 循迹转向 PID。 |
| `/software/line_follow/ark,base-speed-rpm10` | 循迹时 | 0.1 RPM | 循迹基础速度。 |

```powershell
../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe dts.generate --input doc/examples/dts-参数.json
```

## 3. API 参考

| API | 作用 |
|---|---|
| `control_register()` | 注册控制核心。 |
| `control_config_get()` | 返回只读的闭环周期、双轮 PID 与平滑配置。 |
| `control_is_ready()` | 检查 encoder、motor 和控制核心是否可用。 |
| `control_set_speed()` | 切换到 manual 策略并设置左右 `rpm10`。 |
| `control_get_speed()` | 读取目标、实际速度、PWM 和累计距离快照。 |
| `control_update()` | 执行当前策略、速度平滑、双轮 PID 和 motor 输出。 |
| `control_stop()` | 退出策略、清零 PID 并停止 motor。 |
| `control_strategy_register()` | 注册循迹、避障等策略回调。 |
| `control_strategy_select()` / `control_strategy_current()` | 切换或查询当前策略；`manual` 为内置手动模式。 |
| `control_pid_init()` / `control_pid_reset()` / `control_pid_update()` | 可被其它组件复用的纯 PID 算法。 |
| `control_line_tracking_observe()` | 向循迹策略提交位置与有效性观测。 |

## 4. 使用流程

先启用 `encoder`、`motor` 及一个 motor backend，再启用 control。控制任务按 `period-ms` 调用 `control_update()`；业务状态机只选择策略或提交观测，不直接操作 PWM/TB6612。

## 5. 注意事项

- 策略 `update` 只输出目标速度，不访问 HAL 或直接驱动电机。
- 新增避障时实现独立源码并注册 `control_strategy_t`，不要复制速度闭环。
- 策略观测无效或 motor 输出失败时 `control_update()` 返回失败并停止输出。

## 6. 验证

覆盖 PID 边界、策略重复注册、未知策略、manual/line_tracking 切换和无效观测；目标板验证左右方向、周期、停车与故障降级。
