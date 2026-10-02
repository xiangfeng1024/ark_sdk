# lcd 组件用户开发指南

## 1. 组件定位

240x320 RGB565 LCD/ST7789 驱动，提供基础图元、格式化文本、条带区域渲染和传输诊断；依赖 `spi`、`gpio` HAL。

## 2. DTS 配置

```dts
spi1: spi1 {
    compatible = "ark_hal_spi";
    lcd {
        compatible = "lcd";
        bl-gpios = <&gpioa 1 1>;
        cs-gpios = <&gpioa 2 1>;
        dc-gpios = <&gpioa 3 1>;
        reset-gpios = <&gpioa 4 1>;
        status = "okay";
    };
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `compatible` | 是 | 字符串 | 固定为 `"lcd"`。 |
| `bl-gpios` | 是 | GPIO phandle | 背光控制脚。 |
| `cs-gpios` | 是 | GPIO phandle | SPI 片选脚。 |
| `dc-gpios` | 是 | GPIO phandle | 数据/命令选择脚。 |
| `reset-gpios` | 是 | GPIO phandle | 屏幕复位脚。 |
| `status` | 否 | 字符串 | 控制组件是否启用。 |

组件必须位于 SPI provider 下，四组 GPIO 极性需匹配电路。

配置完成后执行 DTS 校验和生成：

```powershell
../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe dts.generate --input doc/examples/dts-参数.json
```

## 3. API 参考

| API | 作用 |
|---|---|
| `lcd_register()` | 注册并加载 SPI/GPIO 配置。 |
| `lcd_clear()` | 使用 RGB565 颜色清屏。 |
| `lcd_fill_rect()` | 填充指定矩形并校验边界。 |
| `lcd_draw_pixel()` | 绘制单个像素。 |
| `lcd_draw_line()` | 绘制指定前景色线段。 |
| `lcd_printf()` | 按位置、缩放和前后景色绘制格式化 ASCII 文本。 |
| `lcd_refresh_region()` | 以 renderer 回调分条渲染指定区域。 |
| `lcd_surface_clear()` | 清空当前区域 surface。 |
| `lcd_surface_draw_pixel()` | 在当前区域 surface 绘制像素。 |
| `lcd_surface_draw_line()` | 在当前区域 surface 绘制线段。 |
| `lcd_get_diagnostics()` | 获取初始化、区域序列、失败和 DMA 字节计数。 |

## 4. 使用流程

配置 SPI 与控制脚后初始化；简单界面直接使用图元，复杂界面通过 `lcd_refresh_region()` 在回调中绘制，避免申请整屏缓冲。

## 5. 注意事项

- 坐标必须位于 240x320 范围，颜色为 RGB565。
- renderer 只在回调提供的 surface 区域内写；不得保留其临时缓冲指针。
- SPI DMA 资源不得与其它外设冲突；多个任务绘制时由上层串行化页面提交。

## 6. 验证

显示纯色、边界矩形、对角线和文本；检查四边无越界/偏移，并观察 diagnostics 中传输失败保持为零。
