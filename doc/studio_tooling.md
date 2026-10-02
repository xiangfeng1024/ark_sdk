# Studio 开发工具与独立参数

开发工具现位于独立 Rust Studio 仓库。本 SDK 提供 App、HAL、组件、catalog 和模板；不再内置 Python Studio。

## 职责

Studio 负责选择独立资源、发现候选、保存本机组合与构造参数。Rust 工具仅接收明确输入。SDK 与 App、工程、Target、Keil 不通过全局变量绑定；执行时检查实际兼容性。选择资源不写入源码。

## 命令行入口

同级克隆 Rust Studio 时可使用：

```powershell
../ark_stdio_rust/ark-studio-cli.exe --list
../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe dts.generate --input doc/examples/dts-参数.json
```

示例参数以当前 SDK 为运行目录，修改 app_dts 与 output_dir 后可用于其它 App。若 Studio 位于其它位置，替换可执行程序路径即可。运行不依赖 Python。GUI 与 CLI 共用实现，失败退出码为 1。

```json
{
  "sdk": ".",
  "app_dts": "app/c8t6_ark_net/c8t6_ark_net.dts",
  "output_dir": "app/c8t6_ark_net"
}
```

编译需要 keil、project、target、log；无需 SDK/App。同步需要 sdk、app_dts、project、target、apply。配置工程额外需要 output_dir，并生成 App 的 OF 文件。apply 默认 false 表示预览。

## 工具行为

工具覆盖 DTS、Keil、创建、克隆、打包、清理、内存、编码、探针、串口与 GCC。每个工具在 tools/sdk 内独立目录维护。任务有真实日志、进度、取消、资源锁和后置检查。同步只修改指定 Target；打包只修改暂存副本，先完整构建再发布。新建和克隆不创建软链接。

## 验证

Rust 工具测试在 Studio 仓库运行。维护 App 的 DTS 输出与旧基线对照；真实构建、独立打包、中文路径和失败边界在隔离副本验证。SDK 本身不包含工具运行时，访问私有 Studio 仓库需要相应权限。
