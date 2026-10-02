---
name: ark-sdk-camera
description: Use or modify the MaixCamPro camera applications, flower-pot vision workflow, calibration UI, and STM32 camera serial protocol.
---

# MaixCamPro Skill

## 先读什么

- `../../camera/MaixCamPro/skills/maixcam-pro/SKILL.md`
- `../../camera/MaixCamPro/flower_pots/protocol.md`
- `../../camera/MaixCamPro/flower_pots/maixpy/main.py`
- `../../camera/MaixCamPro/flower_pots/opencv/main.py`（仅作旧对照）

## 入口与协议

- 生产入口是 `camera/MaixCamPro/flower_pots/maixpy/main.py`；OpenCV 脚本是旧对照版，不能替代生产验证。
- 屏幕侧串口通常为 `/dev/ttyS0`，STM32 USART2 使用 115200、DMA-to-idle RX 和轮询 TX。
- 帧格式为 `5A 5B len command payload A5`，命令和状态机定义以 `protocol.md` 为准；必须处理长度、校验、超时和重复命令。
- 启动、校准、识别、停止、故障和重试是独立状态；STOPPED/FAULT 下只接受明确允许的启动命令，启动后先保持停车再进入循迹。

## 校准与安全

校准模式使用独立黑色画布，显示 Flash 默认值和本轮采样；白底/黑线采样、写入、读取和退出必须由协议状态确认。保存后不要自动启动识别。生产验证使用 480x320 采集/显示基线，并保留丢帧、无目标和重试提示。

修改相机协议或 STM32 接收逻辑时，同时检查 `app/c8t6_microcar_soil` 的 `stream`/`maixcam` 配置与 `camera/MaixCamPro/flower_pots/protocol.md` 协议文档。
