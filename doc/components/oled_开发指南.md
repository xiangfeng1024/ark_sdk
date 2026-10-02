# oled 组件用户开发指南

## 1. 组件定位

SSD1306 128x64 OLED 组件，维护 1024 字节 framebuffer，提供 ASCII/内置中文字形、像素线条、提交和帧复制；依赖 `i2c` HAL。

## 2. DTS 配置

```dts
i2c1: i2c1 {
    compatible = "ark_hal_i2c";
    oled@3c { compatible = "oled"; reg = <0x3c>; status = "okay"; };
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `compatible` | 是 | 字符串 | 固定为 `"oled"`。 |
| `reg` | 是 | 7 位 I2C 地址 | 常见为 `0x3c`。 |
| `status` | 否 | 字符串 | 控制组件是否启用。 |

节点必须位于 I2C provider 下。

配置完成后执行 DTS 校验和生成：

```powershell
python -m studio.cli dts app/<app> --check
python -m studio.cli dts app/<app>
```

## 3. API 参考

| API | 作用 |
|---|---|
| `oled_register()` | 注册并初始化显示器。 |
| `oled_printf()` | 绘制文本并按实现提交。 |
| `oled_canvas_clear()` | 清空内存画布。 |
| `oled_draw_pixel()` | 修改画布像素。 |
| `oled_draw_line()` | 在画布绘制线段。 |
| `oled_draw_zh16()` | 绘制一个内置 16x16 中文字形。 |
| `oled_draw_printf()` | 只绘制格式化 ASCII 文本到画布。 |
| `oled_present()` | 将当前画布提交到 OLED。 |
| `oled_copy_frame(buffer, size, sequence)` | 复制完整 1024 字节帧及序列号。 |

## 4. 使用流程

初始化后每帧先 clear，再调用 draw 系列，最后只调用一次 present；简单文本可直接使用 `oled_printf()`。

## 5. 注意事项

- 坐标范围为 128x64，frame buffer 容量必须至少 `OLED_FRAME_BUFFER_SIZE`。
- 中文接口只支持枚举中内置的 16x16 字形。
- 多任务不得并发修改画布；I2C 传输经 HAL 互斥，避免高频整屏刷新。

## 6. 验证

显示全亮/全灭、四角像素、对角线、ASCII 和内置中文字形；检查地址 ACK、刷新撕裂和 sequence 递增。
