# json 组件用户开发指南

## 1. 组件定位

对 cJSON 的受控封装，提供对象创建、定长解析、类型读取、字段添加和定长序列化。组件无 HAL 依赖，但会使用 cJSON 的内存分配。

## 2. DTS 配置

```dts
json {
    compatible = "json";
    status = "okay";
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `compatible` | 是 | 字符串 | 固定为 `"json"`。 |
| `status` | 否 | 字符串 | 控制 JSON 组件是否加入构建。 |

无专用属性。只有使用 JSON API 的 App 才应启用，以便构建裁剪包含 `json.c` 与 `cJSON.c`。

配置完成后执行 DTS 校验和生成：

```powershell
python -m studio.cli dts app/<app> --check
python -m studio.cli dts app/<app>
```

## 3. API 参考

| API | 作用 |
|---|---|
| `json_register()` / `json_initialize()` | 注册组件或显式初始化封装。 |
| `json_create_object()` / `json_parse(text, length)` | 创建对象或解析指定长度文本，失败返回 `NULL`。 |
| `json_delete(value)` | 释放对象树。 |
| `json_get_bool()` | 按名称读取布尔字段。 |
| `json_get_u32()` | 读取并校验无符号 32 位整数范围。 |
| `json_get_i32()` | 读取并校验有符号 32 位整数范围。 |
| `json_get_double()` | 读取数值字段为 double。 |
| `json_add_bool()` | 增加布尔字段。 |
| `json_add_u32()` | 增加无符号整数字段。 |
| `json_add_double()` | 增加浮点数值字段。 |
| `json_print(value, buffer, capacity, length)` | 序列化到调用方缓冲区并返回实际长度。 |

## 4. 使用流程

初始化后创建或解析对象，逐项检查 get/add 返回值，序列化完成或处理结束后始终调用 `json_delete()`。

## 5. 注意事项

- `json_value_t` 生命周期由调用方负责，错误路径也必须释放。
- 输入未必以 NUL 结尾，应传真实 `length`；输出容量不足时必须处理失败。
- 避免在小栈任务或 ISR 中解析大型 JSON；不要把密码写入日志。

## 6. 验证

覆盖合法对象、缺字段、错误类型、整数越界、截断输入和输出缓冲不足，并运行内存泄漏/重复调用测试。
