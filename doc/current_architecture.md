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

React 界面 → Rust/Tauri 宿主 → 显式参数工具集 → 隐藏的 Keil/OpenOCD/GCC 进程。

Studio 源码在独立私有仓库。SDK、App、工程、Target 和 Keil 分别选择，由 Studio 保存本机组合；工具不读取这些本机状态。DTS 生成不需要工程，编译不需要 SDK/App。同步只修改所选 Target，打包只修改暂存副本。

SDK 目录包括 app、component、hal、doc、camera、skills。旧 Python Studio 已移出正式源码；相机应用脚本继续保留。详见 [工具说明](studio_tooling.md)。

## 常用验证

```powershell
../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe --list  # 在工具参数界面填写目标，CLI 使用对应工具 ID 与 JSON 参数
cargo test --manifest-path ../ark_stdio_rust/tools/sdk/Cargo.toml
../ark_stdio_rust/Ark Studio.exe
```
