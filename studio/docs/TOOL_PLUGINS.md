# Studio内置工具规范

工具清单位于`studio/resources/tools/`并符合`studio/resources/schemas/tool.schema.json`。清单只允许`entrypoint.kind=builtin`和稳定的`handler`，禁止脚本路径、文件路径和任意模块路径。

handler由`studio.brain.tasks`映射到`studio.tooling`中的可导入模块。Studio任务直接调用Python API；Keil、OpenOCD、DTC等外部程序必须通过隐藏进程运行器执行。

新增工具时必须同时提供清单、handler实现、参数校验、资源锁、进度、取消策略和测试。运行：

```powershell
python -m pytest studio/tests -q
python -m studio --check --workspace .
```
