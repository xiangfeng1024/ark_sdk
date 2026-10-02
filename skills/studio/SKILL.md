---
name: studio
description: 开发独立 Rust 方舟 Studio 的 React 界面、资源组合、任务日志、串口和便携发布。
---

# 方舟 Studio

界面和宿主在独立私有仓库，同级路径 `../../../ark_stdio_rust/`。先读该仓库 AGENTS.md、DESIGN.md 和 tools/sdk/README.md；SDK 工具规则见 `../tooling/SKILL.md`。

资源选择、本机组合、兼容提示属于 Studio；工具只使用显式参数与任务快照。React 不直接调用系统命令，Rust 宿主运行后台工具并隐藏外部进程。保留蓝色主题、中文离线字体、卡片、IconAction 与工作台监控台。

验证前端、Rust、界面截图和真实桌面启动/退出。发布脚本只构建便携版，通过检查后把 EXE、CLI 和运行库复制到项目根目录；不提交产物。SDK 内的旧 Rust/React 实现已经移出正式源码。
