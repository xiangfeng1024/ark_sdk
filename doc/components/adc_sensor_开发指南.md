# adc_sensor 组件用户开发指南

## 1. 组件定位

通用 ADC 传感器集合，当前支持 `mq7`、`mq135` 和 `soil` 三种逻辑实例，输出 12 位原始值、毫伏值和采样 tick；依赖 `adc` HAL。

## 2. DTS 配置

```dts
adc1: adc1 {
    compatible = "ark_hal_adc";
    #io-channel-cells = <1>;
    adc_sensor@9 {
        compatible = "adc_sensor";
        reg = <9>;
        label = "soil";
        status = "okay";
    };
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `compatible` | 是 | 字符串 | 固定为 `"adc_sensor"`。 |
| `reg` | 是 | ADC 通道号 | 父 ADC provider 中的通道。 |
| `label` | 是 | 字符串 | `mq7`、`mq135` 或 `soil`。 |
| `status` | 否 | 字符串 | 控制该实例是否启用。 |

节点必须是 ADC provider 子节点。最多配置 3 个实例，名称不得重复。

配置完成后执行 DTS 校验和生成：

```powershell
../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe dts.generate --input doc/examples/dts-参数.json
```

## 3. API 参考

| API | 作用 |
|---|---|
| `adc_sensor_register()` | 注册组件和采样 CLI。 |
| `adc_sensor_read(kind, sample)` | 按 `adc_sensor_kind_t` 读取传感器；成功返回 `raw`、`millivolts` 和 `tick`。 |

## 4. 使用流程

在 ADC provider 下建立实例，生成 DTS 后由框架初始化；业务代码选择枚举调用 `adc_sensor_read()`，并检查返回值后再使用数据。

## 5. 注意事项

- 毫伏换算按 3.3 V、12 位 ADC 处理，外部分压和传感器曲线由应用层换算。
- MQ 类传感器需要预热，组件不判断气体浓度是否稳定。
- 不允许多个实例占用相同 ADC 资源。

## 6. 验证

执行 DTS/pytest 检查；在目标板短接 GND、参考电压及实际传感器，确认 raw/mV 范围和实例映射正确。
