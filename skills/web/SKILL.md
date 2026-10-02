---
name: ark-sdk-web
description: Route ARK CREW device Web service requests to the independent ark_web repository and verify implementation availability before making changes.
---

# Web 服务路由

Web 服务属于同级独立 `ark_web` 仓库，不在本 SDK 内。当前 ark_web 为空，没有可运行的服务、API、数据库或 Docker 配置。不得按历史文档假定 FastAPI/SQLite 服务已经存在，或调用已移除的 web/ark 路径。

收到服务开发请求时，先检查实际 ark_web 源码与 README；为空时按用户需求在该仓库设计实现。有实现后，以实际代码确定认证、设备协议、数据库和部署方式，使用该仓库真实测试命令验证。设备协议先参考 SDK 的 component/ark_net、component/wifi 及 doc/components 对应指南。不要提交用户密码、运行数据库、日志或部署凭据。
