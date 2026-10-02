# watchdog 组件用户开发指南

## 1. 组件定位

系统看门狗组件，接入 `ark_hal_watchdog`，用于统一启动自检和受控软件复位；依赖 `watchdog` HAL。

## 2. DTS 配置

```dts
iwdg: iwdg {
    compatible = "ark_hal_watchdog";
    ark,logical-id = "watchdog1";
    status = "okay";
};
watchdog { compatible = "watchdog"; status = "okay"; };
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| provider `compatible` | 是 | 字符串 | 固定为 `"ark_hal_watchdog"`。 |
| provider `ark,logical-id` | 是 | 字符串 | Watchdog HAL 逻辑 ID。 |
| 组件 `compatible` | 是 | 字符串 | 固定为 `"watchdog"`。 |
| `status` | 否 | 字符串 | provider 和组件都必须启用。 |

超时时间、句柄与 CubeMX 头文件由 provider/工程配置，组件节点无专用属性。

配置完成后执行 DTS 校验和生成：

```powershell
../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe dts.generate --input doc/examples/dts-参数.json
```

## 3. API 参考

| API | 作用 |
|---|---|
| `watchdog_register()` | 注册系统看门狗组件。 |
| `watchdog_request_reset()` | 停止正常喂狗路径并请求由看门狗触发复位。 |

## 4. 使用流程

将 watchdog 放在 Level 1 启动基础中；确保长初始化和自检能在超时预算内喂狗，致命故障时调用 request_reset 并等待复位。

## 5. 注意事项

- IWDG 启动后通常不能关闭，调试和低功耗策略必须提前设计。
- 所有可能超过超时的 Flash、网络和传感器操作都要拆分或合理喂狗。
- request_reset 后不要继续执行依赖一致性的业务写操作。

## 6. 验证

分别验证正常持续运行、故意停止喂狗和 request_reset；读取复位原因并确认不会形成无法诊断的快速复位循环。
