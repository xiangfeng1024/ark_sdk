---
name: tooling
description: 使用和扩展独立 Rust SDK 开发工具，适用于 DTS、Keil、创建、同步、清理、打包、编码、内存和串口。
---

# SDK Rust 工具

先读 `../../doc/studio_tooling.md`。实际工具位于独立 Studio 仓库 `tools/sdk/`，同级检出时为 `../../../ark_stdio_rust/tools/sdk/`。该仓库为私有，需要相应访问权限；不要假设 SDK 内存在 Python Studio。

工具通过稳定 ID 与 UTF-8 JSON 参数执行，GUI 和 CLI 共用 Rust 实现。源码事实是工具 Input、catalog、DTS 和测试。显式传入 SDK/App/工程/Target，禁止隐式同名工程发现与全局 SDK 状态。

DTS 校验/生成不需要 Keil；编译不需要 SDK 或 App。同步只修改指定 Target。写操作先预览，测试使用隔离副本；清理不删除厂商库，克隆不生成软链接，打包只修改暂存包并验证完整构建。检查真实日志和产物，缺少硬件如实报告。

在 Studio 仓库运行 `cargo test --manifest-path tools/sdk/Cargo.toml`。具体 CLI 参数示例见上述文档。
