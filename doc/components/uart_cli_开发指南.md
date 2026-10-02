# uart_cli 组件用户开发指南

## 1. 组件定位

基于 `ark_stream` 的交互式命令行终端，负责欢迎文本、行编辑、历史、命令查找和标准输出重定向；不管理物理 UART。

## 2. DTS 配置

```dts
uart_cli {
    compatible = "uart_cli";
    ark,stream-name = "cli";
    ark,welcome = "ARK CREW ready";
    status = "okay";
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| `compatible` | 是 | 字符串 | 固定为 `"uart_cli"`。 |
| `ark,stream-name` | 是 | 字符串 | CLI 绑定的 stream 名称。 |
| `ark,welcome` | 是 | 字符串 | CLI 启动欢迎文本。 |
| `status` | 否 | 字符串 | 控制组件是否启用。 |

同一 App 必须另有相同名称的 stream，并确保 CLI 是该 stream 的唯一读取者。

配置完成后执行 DTS 校验和生成：

```powershell
python -m studio.cli dts app/<app> --check
python -m studio.cli dts app/<app>
```

## 3. API 参考

| API | 作用 |
|---|---|
| `uart_cli_config_get()` | 返回只读 stream 名称和欢迎文本配置。 |
| `uart_cli_register()` | 注册 CLI 组件及内置 help/hello 命令。 |
| `uart_cli_start()` | 在组件初始化后启动 CLI 任务。 |
| `uart_cli_write(data, size)` | 向 CLI stream 写原始数据，返回字节数或负值。 |

## 4. 使用流程

先启用 stream，再注册并完成统一组件初始化，最后由 App 显式调用 `uart_cli_start()`；各组件在自己的 register 中注册静态命令。

## 5. 注意事项

- 不要在 CLI 命令处理器中长期阻塞；重任务交给工作任务。
- CLI 启动失败不能伪装成功，欢迎文本仅在 stream 就绪后发送。
- 日志与命令输出共享 stream 时要考虑行编辑显示和并发串行化。

## 6. 验证

通过串口测试 help、未知命令、退格、历史和长行边界；确认 stream 未就绪时 start 失败可观测。
