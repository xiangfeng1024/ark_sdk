---
name: studio
description: 开发、调试和测试ARK CREW PySide6 Studio，包括主窗口、工具覆盖层、Brain协议、任务日志、串口、产物、设置、字体、图标和EXE打包。
---

# ARK CREW Studio

开始前阅读`../../AGENT.md`、`../../doc/studio_tooling.md`、`../../studio/docs/ARCHITECTURE.md`和`../../studio/docs/DESIGN.md`。Python工具链规则见`../tooling/SKILL.md`。

## 架构

- Studio仅使用PySide6 Qt Widgets，源码包为`studio`。
- `BrainClient`通过QProcess启动`python -m studio --brain-worker`，冻结后启动同一EXE的`--brain-worker`模式。
- JSONL协议固定为UTF-8和版本2；stdout只能输出协议数据。
- 工具来自`studio/resources/tools/*.json`，工具实现从`studio.tooling`直接import。
- 后台外部程序必须使用隐藏进程参数；用户主动打开的编辑器和资源管理器可以显示。

## UI规则

- 默认1440×900，最小1080×680；监控日志默认展开。
- 中文字体使用内置Noto Sans SC；窗口、任务栏和托盘使用标准ARK CREW图标。
- 工具参数和危险确认使用主窗口内覆盖层，不创建顶层QDialog。
- 工具列表、参数、禁用原因和资源锁必须来自清单和Brain，不在界面复制业务配置。

## 验证

```powershell
python -m pytest studio/tests -q
python -m studio --check --workspace .
python -m studio --build
```

检查冻结版UTF-8中文、双击启动、隐藏控制台、1440×900和1080×680截图、窗口图标、串口测试模式及退出清理。真实串口、Keil和探针结果必须如实记录。
