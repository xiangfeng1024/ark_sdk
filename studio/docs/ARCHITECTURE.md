# PySide6 Studio架构

## 进程结构

```text
PySide6 QMainWindow
  -> BrainClient / QProcess
    -> ARKCrewStudio.exe --brain-worker
      -> studio.brain UTF-8 JSONL protocol 2
        -> workspace / registry / SDK check / tasks / serial / artifacts
```

源码运行时子进程使用`python -m studio --brain-worker`；冻结后使用同一EXE。工具清单只声明内置handler，Brain直接import `studio.tooling`中的callable，不再启动Python脚本子进程。外部工作区路径写入`ARK_SDK_ROOT`，因此catalog、DTS和工程配置始终来自用户选择的SDK副本。

## 页面

- 工程控制台：标题区内嵌App/SDK状态、自适应动态工具卡、工具任务日志和真实进度星环。
- 最近任务：任务列表及最多两个结构化日志并排比较。
- 串口终端：单会话UTF-8/GBK/ASCII/HEX收发、键盘直连、ANSI方向键、控制字节、暂停滚动和导出。
- 构建产物：固件、MAP、日志和DTS生成文件列表及受限文本预览。
- SDK环境检查：自动快检和手动只读深检。冻结版直接读取内置Python运行时版本，不递归启动Studio EXE；未连接可选硬件记为警告，依赖该硬件的工具保持不可用。
- 设置：探针、历史保留、时间戳和通知。

窗口底部使用单行状态栏汇总Brain连接、运行队列、警告和错误；Studio日志按需展开并提供级别过滤与诊断复制，仅记录Brain连接、工作区、设置和应用异常，不承载工具任务输出。当前工具的任务抽屉独立提供摘要、问题、原始输出和产物视图。

## Brain增量接口

- `serial.open/close/write/snapshot`，事件为`serial.opened/rx/tx/error/closed`。
- `artifact.list/read`，预览上限2 MiB且只能读取已发现的文本产物。
- 工具清单`ui_mode`默认为`task`，`serial_terminal`由原生串口页承接。
- 任务和串口共享资源锁；协议版本保持2。
- Keil、OpenOCD和DTC等外部程序统一通过隐藏进程运行器启动；VS Code和资源管理器仅在用户明确操作时显示。
- 点击工具卡主体始终打开窗口内详情层，只有明确点击执行按钮才创建任务。`parameter_mode=none`的卡片执行按钮直接启动任务；固定参数和动态参数在详情层内编辑后执行。
- Studio 和 Brain 同时执行全局单任务约束。工具运行期间其它执行入口禁用，但详情与参数保持可查看。
