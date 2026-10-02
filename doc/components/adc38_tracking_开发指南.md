# adc38_tracking 组件用户开发指南

## 1. 组件定位

8 路模拟循迹阵列驱动。组件通过一个 ADC 通道和 3 根选通 GPIO 轮询 8 路输入，输出原始值、归一化值、线位置和有效掩码；依赖 `adc`、`gpio`、`time` HAL。

## 2. DTS 配置

```dts
adc38_tracking {
    compatible = "adc38_tracking";
    io-channels = <&adc1 8>;
    select-gpios = <&gpioc 13 0>, <&gpiob 3 0>, <&gpiob 4 0>;
    status = "okay";
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `compatible` | 是 | 字符串 | 固定为 `"adc38_tracking"`。 |
| `io-channels` | 是 | ADC phandle + 通道 | 复用器输出连接的 ADC 通道。 |
| `select-gpios` | 是 | 3 个 GPIO phandle | 从低位到高位的 3 根选通线。 |
| `status` | 否 | 字符串 | 缺省/`okay` 启用，`disabled` 禁用。 |
| `/software/calibration/ark,white` | 是 | 8 个 u32 | 白底基准。 |
| `/software/calibration/ark,black` | 是 | 8 个 u32 | 黑线基准。 |
| `ark,samples-per-channel` | 是 | 次数 | 每路平均采样次数。 |
| `ark,settle-us` | 是 | 微秒 | 切换通道后的稳定时间。 |

配置完成后执行 DTS 校验和生成：

```powershell
../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe dts.generate --input doc/examples/dts-参数.json
```

## 3. API 参考

| API | 作用 |
|---|---|
| `adc38_tracking_register()` | 注册组件和诊断 CLI。 |
| `adc38_tracking_read(sample)` | 完成 8 路扫描并输出归一化位置快照。 |
| `adc38_tracking_scan_raw(raw)` | 只获取 8 路原始 ADC 值。 |
| `adc38_tracking_calibrated()` | 查询当前校准是否有效。 |
| `adc38_tracking_get_calibration()` | 读取当前白/黑基准。 |
| `adc38_tracking_set_calibration()` | 校验并更新白/黑基准。 |
| `adc38_tracking_capture_white()` | 采集一组白底基准。 |
| `adc38_tracking_capture_black()` | 采集一组黑线基准。 |

## 4. 使用流程

配置 ADC/provider、选通 GPIO 和校准节点，运行 `Rust DTS 生成工具 --check` 与生成器；框架初始化成功后先确认校准，再周期调用 `adc38_tracking_read()`。

## 5. 注意事项

- 固定 8 通道；数组容量必须为 `ADC38_TRACKING_CHANNEL_COUNT`。
- `settle-us` 太短会串扰，采样次数太大会拉长控制周期。
- 校准白值与黑值不得相同；更改校准后应持久化到 App 配置或 Flash。

## 6. 验证

运行 DTS/pytest 检查，并在目标板执行组件 CLI；分别置于白底、黑线和边界位置，确认 `valid_mask`、`line_mask` 与 `position` 连续变化。
