# dht11 组件用户开发指南

## 1. 组件定位

DHT11 单总线温湿度驱动，负责微秒级起始、应答、40 位数据和校验和处理；依赖 `gpio`、`time` HAL。

## 2. DTS 配置

```dts
dht11 {
    compatible = "dht11";
    data-gpios = <&gpiob 5 0>;
    status = "okay";
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `compatible` | 是 | 字符串 | 固定为 `"dht11"`。 |
| `data-gpios` | 是 | GPIO phandle | 单总线数据脚及有效电平。 |
| `status` | 否 | 字符串 | 控制组件是否启用。 |

数据线应按开漏方式接入并上拉。配置后运行 `python -m studio.cli dts app/<app> --check` 和生成命令。

配置完成后执行 DTS 校验和生成：

```powershell
python -m studio.cli dts app/<app> --check
python -m studio.cli dts app/<app>
```

## 3. API 参考

| API | 作用 |
|---|---|
| `dht11_register()` | 注册组件和诊断 CLI。 |
| `dht11_read(sample)` | 执行一次采样；温度为 `temperature_deci_c`，湿度为 `humidity_deci_percent`。 |

## 4. 使用流程

完成 GPIO 配置并初始化后，在低频传感任务中调用 `dht11_read()`；仅在返回 `true` 时发布新样本。

## 5. 注意事项

- DHT11 不适合高频读取，建议间隔至少 1 秒。
- 微秒时序期间不能在 ISR 调用，系统中断延迟过大会造成校验失败。
- 长线需检查上拉、电平和波形；校验失败时不得复用未标记的新数据。

## 6. 验证

执行 DTS/pytest 与 DHT11 CLI；连续读取并检查校验失败率，必要时用逻辑分析仪核对起始脉冲和 40 位时序。
