# motor_can 组件用户开发指南

## 1. 组件定位

`motor` 类的 CAN/M3508 后端，负责 CAN 初始化、四路电流帧和回环自检；不参与速度算法。

## 2. DTS 配置

```dts
can1: can1 {
    compatible = "ark_hal_can";
    motor {
        compatible = "motor";
        motor_can { compatible = "motor_can"; status = "okay"; };
    };
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `compatible` | 是 | 字符串 | 固定为 `"motor_can"`。 |
| CAN provider 祖先 | 是 | `ark_hal_can` | 提供实际 CAN ID。 |
| 父节点 | 是 | `motor` | 后端不能独立存在。 |

```powershell
python -m studio.cli dts app/<app> --check
python -m studio.cli dts app/<app>
```

## 3. API 参考

| API | 作用 |
|---|---|
| `motor_can_register()` | 注册 CAN 后端并在初始化后附加到 motor。 |

M3508 电流通过 `motor_set_currents()` 发送，停止使用 `motor_stop()`。

## 4. 使用流程

完成 CubeMX CAN、过滤器、引脚和终端电阻，再配置 motor/motor_can 节点；上层只调用 motor 核心。

## 5. 注意事项

- API 提交成功不代表电机已执行，需要另行处理反馈帧。
- 自检使用 loopback，结束后恢复正常模式；电流范围遵守 C620/M3508 手册。

## 6. 验证

先执行回环自检，再用 CAN 分析仪核对 `0x200` 帧和 8 字节大端电流顺序。
