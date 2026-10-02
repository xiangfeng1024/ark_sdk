---
name: ark-embedded-sdk
description: 开发、配置和审查方舟分层 SDK、DTS、组件、HAL、App 与独立 Rust 开发工具。
---

# 方舟 SDK 开发入口

先读 ../../AGENT.md 和 ../../doc/current_architecture.md；硬件基线与历史排查见 ../../doc/ark_sdk_guide.md。修改维护 App 的业务前阅读项目需求。以真实 DTS、catalog、头文件、IOC、Keil 配置与测试为事实，文档不覆盖源码。

按任务读取 ../hal、../component、../app、../camera、../tooling、../studio 下的 SKILL.md。Web 位于独立 ark_web 仓库，当前为空，不推测服务实现。

App DTS 描述 /sys 硬件和 /software 业务。新建资源保持统一名，既有资源由 Studio 明确组合，兼容性在执行时检查。不得在 DTS 加机器绝对路径或自动推断 Keil 工程。CubeMX C/H 只读，OF 文件用 Rust 工具生成。

组件保持可复用，产品算法与调度在 App。HAL 实现只在选定平台层，构建选型仅来自 catalog；禁用节点不生成，公共 component/common 源始终纳入。初始化和通信结果必须真实，保持协议字段、DMA 生命周期、中断规则和资源唯一性。

工具与 UI 位于独立 Rust Studio 仓库，使用显式参数，禁止隐藏的全局 SDK 根目录。修改工具先读 ../tooling/SKILL.md；移植结果需用实际源码与产物对照。无硬件测试时如实记录，不把桩结果视为实测。
