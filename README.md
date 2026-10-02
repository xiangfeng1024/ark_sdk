# 方舟小队 SDK

本仓库维护 STM32/GD32 App、HAL、组件、catalog、DTS 与开发文档。桌面工具在独立 Rust Studio 仓库，开发板 CubeMX/Keil 工程在独立 STM32 仓库。

## 使用

在 Studio 分别选择 SDK、App DTS、开发板工程、Target 和 Keil，可保存本机组合。各仓库可放在不同目录，不需要软链接。普通 Keil 编译无需 App 或 SDK，DTS 校验与生成无需开发板工程。

维护中的 App 是 c8t6_microcar_soil 和 c8t6_ark_net；后者默认对应网络板 c8t6_xiaoyan_net，但该关联仅是推荐，可以明确选择兼容工程。其它 JSON App 是历史示例。

工具使用和 JSON 参数示例见 [开发工具说明](doc/studio_tooling.md)。组件文档与代码模板集中在 doc/components 和 doc/component_template，配置 schema 在 doc/schemas。

## 目录

- app：业务源码、App DTS 与项目需求。
- hal：平台适配和 HAL catalog。
- component：组件源码、公共 OF 框架和组件 catalog。
- camera：独立相机脚本与协议。
- doc：开发说明、组件指南、模板与 schema。
- skills：项目开发路由与约束。

旧 Python Studio 已迁移到 Rust 工具并从正式源码移除。相机 Python 脚本属于目标设备应用，继续保留。

## 开发与授权

App DTS 是配置源；生成文件通过 Rust 工具更新，不手工修改 CubeMX 代码。遵循 [开发规则](AGENT.md)，按特性分支与 PR 协作。

保留 LICENSE.txt 与第三方授权。本仓库公开可见不代表变更原非商业许可。构建日志、凭据、缓存和发布程序不上传。已知构建限制和迁移验证见 [验证记录](doc/SOURCE-VERIFICATION.md)。
