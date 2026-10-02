# ARK CREW Studio 0.3.0

`studio`是SDK默认桌面工作台，使用PySide6 Qt Widgets实现。它通过UTF-8 JSONL协议2连接独立`studio.brain`进程，支持动态工具、只读SDK深检、结构化任务日志、内嵌串口Shell、双任务日志比较和构建产物预览。所有Python工具均内置在`studio.tooling`并通过import调用。

```powershell
python -m pip install -r studio/requirements.txt
python -m studio --check --workspace .
python -m studio --workspace .
python -m studio --build
python -m studio.cli --help
```

发布目录为`studio/dist/ARKCrewStudio-0.3.0-windows-x64/`，目标机无需安装Python。用户设置和日志保存在`%LOCALAPPDATA%/ARKCrewStudio`。

串口终端支持UTF-8、GBK、ASCII和HEX显示。连接后启用“键盘直连”并点击终端区域，可以发送普通字符、方向键ANSI序列、Enter、退格、Tab及`Ctrl+A`到`Ctrl+Z`；`Ctrl+Shift+C/V`用于本地复制粘贴。本地回显默认关闭，避免目标板回显时出现重复字符。

架构见[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)，视觉规范见[docs/DESIGN.md](docs/DESIGN.md)。
