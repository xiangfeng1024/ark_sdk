# ARK CREW Studio工具链

Studio、Brain和Python工具链统一位于`studio/`。发布版EXE不要求目标机安装Python。

## 入口

```powershell
python -m pip install -r studio/requirements.txt
python -m studio --check --workspace .
python -m studio --workspace .
python -m studio --build
python -m studio.cli --help
```

常用模块命令：

```powershell
python -m studio.cli dts app/c8t6_microcar_soil --check
python -m studio.cli dts app/c8t6_microcar_soil
python -m studio.cli configure app/c8t6_microcar_soil/c8t6_microcar_soil.dts --dry-run
python -m studio.cli component-create --sdk-root . --name example --hal gpio --check
python -m studio.cli project audit-paths ..
python -m studio.cli probe --check --probe dap --device STM32F103C8 --json
```

## 内置工具

工具清单位于`studio/resources/tools/`，只声明稳定handler。Brain从`studio.tooling`直接import实现，禁止通过源码路径启动Python脚本。工具必须声明参数模式、环境检查、进度、资源锁、取消策略和后置校验。

Keil、OpenOCD和DTC是必要的外部程序，通过隐藏进程运行器执行。VS Code和Windows资源管理器只在用户明确打开时显示。串口默认使用Studio内嵌终端；命令行模式使用`python -m studio.cli serial --interactive`。

## 发布与验证

发布目录为`studio/dist/ARKCrewStudio-0.3.0-windows-x64/`。验证至少包括：

```powershell
python -m pytest studio/tests -q
python -m studio --check --workspace .
python -m studio.cli dts app/c8t6_ark_net --check
git diff --check
```

真实Keil、串口和探针结果必须按本机硬件状态记录，不能用测试模式代替硬件验证。
