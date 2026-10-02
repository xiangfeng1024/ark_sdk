# maixcam 组件用户开发指南

## 1. 组件定位

MaixCam 串口协议组件，通过父级 `stream` 解析 `5A 5B` 长度帧，发布花朵检测结果和命令请求，并发送状态响应。

## 2. DTS 配置

```dts
stream_maixcam: stream {
    compatible = "stream";
    ark,stream-name = "maixcam";
    current-speed = <115200>;
    maixcam { compatible = "maixcam"; status = "okay"; };
};
```

| 属性 | 必填 | 类型/单位 | 说明 |
|---|---|---|---|
| 子节点 `compatible` | 是 | 字符串 | 固定为 `"maixcam"`。 |
| 父节点 `ark,stream-name` | 是 | 字符串 | MaixCam 协议绑定的 stream。 |
| 父节点 `current-speed` | 是 | bit/s | 与摄像头端一致的波特率。 |
| `status` | 否 | 字符串 | 控制协议组件是否启用。 |

配置完成后执行 DTS 校验和生成：

```powershell
python -m studio.cli dts app/<app> --check
python -m studio.cli dts app/<app>
```

## 3. API 参考

| API | 作用 |
|---|---|
| `maixcam_register()` | 注册协议组件。 |
| `maixcam_get_result()` | 获取检测标记、中心、宽高和 tick。 |
| `maixcam_get_command()` | 获取最新命令、序列号和 tick。 |
| `maixcam_send_response()` | 发送命令状态及不超过 32 字节的 payload。 |
| `maixcam_notify_task_ready()` | 发送任务就绪响应。 |
| `maixcam_read_raw()` | 从内部缓存读取原始字节，返回字节数或负值。 |

## 4. 使用流程

先启用 UART/stream；后台解析后应用按 sequence 处理命令，读取视觉结果，完成动作后用对应 command/status 回复。

## 5. 注意事项

- 最大 payload 为 32 字节；检测无效时中心和宽高必须为零。
- stream 不得被其它消费组件同时读取；协议解析不在 ISR 中执行。
- 使用 tick 判断结果是否过期，不能把旧帧当作实时检测。

## 6. 验证

回放正确帧、错误头尾、非法长度和乱序字节；目标板与 MaixCam 联调命令 sequence、任务就绪和响应状态。
