---
name: tooling
description: 使用、扩展和排查ARK CREW Studio内置Python工具链，适用于DTS、Keil、组件/App创建、工程管理、串口、打包、内存分析、工具清单和Brain任务。
---

# Studio Python工具链

事实来源为`../../studio/tooling/`、`../../studio/brain/`、`../../studio/resources/tools/`和`../../studio/tests/`。工具必须通过包导入和稳定handler调用。

## 调用方式

Studio内部通过handler直接import Python模块。CI和无界面操作使用：

```powershell
python -m studio.cli --help
python -m studio.cli dts app/c8t6_microcar_soil --check
python -m studio.cli configure app/c8t6_microcar_soil/c8t6_microcar_soil.dts --dry-run
python -m studio.cli component-create --sdk-root . --name example --hal gpio --check
```

## 工具规则

- 清单只允许`entrypoint.kind=builtin`和稳定handler，禁止脚本、文件和模块路径。
- Python工具应提供可复用函数；命令行`main()`只负责参数解析。
- Brain任务必须报告结构化进度、日志、资源锁、取消结果和后置校验。
- Keil、OpenOCD、DTC等外部进程使用`studio.tooling.process_runner`隐藏窗口并捕获输出。
- VS Code和资源管理器仅在用户明确请求时显示。
- 所有源码和协议使用UTF-8；不得用`errors=replace`掩盖JSONL协议错误。

## 验证

```powershell
python -m pytest studio/tests -q
python -m studio --check --workspace .
python -m studio.cli dts app/c8t6_ark_net --check
python -m studio.cli project audit-paths ..
```

有副作用的构建、烧录、清理、克隆和打包测试必须使用临时工程、dry-run或明确测试模式，不得操作真实用户输出。
