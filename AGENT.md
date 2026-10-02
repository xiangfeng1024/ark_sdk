# ARK CREW SDK 项目开发规则

## 路由

HAL、驱动、CubeMX 绑定先读 skills/hal/SKILL.md；组件、DTS 与设备先读 skills/component/SKILL.md；开发工具先读 skills/tooling/SKILL.md；桌面界面先读 skills/studio/SKILL.md；App 业务先读 skills/app/SKILL.md；MaixCamPro 先读 skills/camera/SKILL.md。设备 Web 服务位于独立 ark_web 仓库，当前为空。跨层任务读取涉及的全部规则。

## 架构

- App DTS 是业务与设备配置源，禁用节点不参与生成和选型。新建 App 保持 App、DTS、IOC、工程和 Target 同名；既有资源可由 Studio 独立组合，执行时验证芯片及 catalog 兼容性，不隐式绑定同名工程。
- CubeMX 生成的 Core、Drivers、Middlewares C/H 不得手改。ark_dts_generated.c/.h 必须由独立 Rust DTS 工具生成。
- 层级方向是 App → OF/组件 → ark_hal → 平台适配 → 厂商 HAL。
- HAL 与组件的构建选型仅来自各自 catalog；component/common/*.c 始终纳入。
- 工具位于独立 Rust Studio 仓库，当前 SDK 不包含 Python Studio。工具接收明确参数，GUI 与 CLI 共用实现，不依赖本机组合配置。
- 成功必须验证日志或产物；只看进程退出码不够。

## 使用与验证

阅读 doc/studio_tooling.md。DTS 校验不需要工程和 Keil，普通工程编译不需要 SDK/App。构建、烧录、清理、克隆和打包通过明确目标执行；开发测试使用隔离副本，真实硬件结果单独记录。

```powershell
../ark_stdio_rust/ark-studio-cli.exe dts.check --input doc/examples/dts-参数.json
../ark_stdio_rust/ark-studio-cli.exe dts.generate --input doc/examples/dts-参数.json
cargo test --manifest-path ../ark_stdio_rust/tools/sdk/Cargo.toml
```

文档或技能变更后，验证全部技能、Markdown 引用、JSON、工具路由和源码行为，由独立 agent 复核；发现问题修正后重复相关验证。Windows 验证显式使用 UTF-8。

## GitHub

提交说明、PR 和自有帮助文档使用中文 UTF-8，保留许可证和标准字段。后续开发使用特性分支及 PR。保留 .githooks/commit-msg 的 Change-Id。禁止提交凭据、日志、缓存和发布程序。历史仅在用户明确要求时重写，先备份，记录远程提交并使用带明确旧提交编号的 force-with-lease。
