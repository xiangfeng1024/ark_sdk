# ARK CREW SDK当前架构

## 分层

```text
App业务任务
  -> 组件公共API与组件类核心
    -> ark_hal_*抽象
      -> 平台适配与CubeMX句柄
        -> 厂商HAL和硬件
```

`app/<name>/<name>.dts`是App唯一配置源。组件和HAL构建选择只来自`component/common/component_catalog.json`与`hal/common/hal_catalog.json`。`ark_dts_generated.c/.h`由Studio工具链生成，禁止手工修改。

## Studio

```text
PySide6 QMainWindow
  -> BrainClient / QProcess
    -> python -m studio --brain-worker
      -> UTF-8 JSONL协议2
        -> workspace / registry / SDK check / tasks / serial / artifacts
          -> import studio.tooling
```

Studio源码、Brain、工具清单和Python工具均位于`studio/`。工具执行通过import调用；Keil、OpenOCD和DTC等外部程序统一隐藏控制台。任务和串口共享资源锁，任务成功必须经过产物或日志后置校验。

## 目录

```text
app/                 App、DTS和生成OF文件
component/           独立组件及组件类核心
hal/                 HAL抽象和平台适配
doc/                 总览、组件指南、代码模板和SDK配置Schema
studio/              PySide6、Brain、Python工具、清单、测试和EXE输出
```

## 常用验证

```powershell
python -m studio.cli dts app/c8t6_microcar_soil --check
python -m studio.cli configure app/c8t6_microcar_soil/c8t6_microcar_soil.dts --dry-run
python -m pytest studio/tests -q
python -m studio --check --workspace .
```
