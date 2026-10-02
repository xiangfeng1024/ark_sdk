# led 组件用户开发指南

## 1. 组件定位

多实例数字 LED 组件，以 `reg` 作为稳定 ID，提供初始化、自检、开关、翻转和数量查询；依赖 `gpio` HAL。

## 2. DTS 配置

```dts
leds {
    compatible = "led";
    led@0 { reg = <0>; gpios = <&gpioc 13 0>; status = "okay"; };
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| 父节点 `compatible` | 是 | 字符串 | 固定为 `"led"`。 |
| 子节点 `reg` | 是 | 非负整数 | 稳定 LED ID，必须唯一。 |
| 子节点 `gpios` | 是 | GPIO phandle | 控制脚及有效电平。 |
| `status` | 否 | 字符串 | 可分别禁用父节点或子实例。 |

启用实例数量不能超过驱动容量。

配置完成后执行 DTS 校验和生成：

```powershell
../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe dts.generate --input doc/examples/dts-参数.json
```

## 3. API 参考

| API | 作用 |
|---|---|
| `led_register()` | 注册组件。 |
| `led_init()` / `led_self_test()` | 由框架调用，也可用于受控诊断。 |
| `led_set_by_id(id, on)` | 按 DTS `reg` 设置 LED。 |
| `led_toggle_by_id(id)` | 翻转指定 LED。 |
| `led_count()` | 返回已配置实例数。 |

## 4. 使用流程

添加父节点与子实例，生成 DTS；框架自检完成后使用稳定 ID 控制，不依赖子节点排列顺序。

## 5. 注意事项

- GPIO 极性由 phandle flags 表示，板载 LED 常为低电平点亮。
- ID 不等于数组下标；未配置 ID、重复 ID 和 disabled 子节点必须正确处理。
- 自检会改变 LED 状态，启动界面需考虑该短暂变化。

## 6. 验证

测试多个 ID、低有效 LED、越界/缺失 ID 和 disabled 子节点；目标板确认自检及 set/toggle 与实际灯一致。
