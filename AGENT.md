# ARK CREW SDK 项目级 Agent 指南

本文件是 `ark_sdk` 的项目级规则入口。处理本仓库任务时先阅读本文件，再按任务类型读取对应 skill；文档不能覆盖当前源码、DTS、catalog 或测试的事实。

## 快速路由

| 任务 | 必读 skill | 事实来源 |
|---|---|---|
| HAL、驱动、DMA、IRQ、CubeMX 绑定 | `skills/hal/SKILL.md` | `doc/current_architecture.md`、`hal/include/`、`hal/common/hal_catalog.json` |
| 组件、DTS OF、传感器、执行器、显示、协议 | `skills/component/SKILL.md` | `component/common/component_catalog.json`、组件头文件、对应 App DTS |
| Python 脚本、DTS/Keil、工具清单、Brain | `skills/tooling/SKILL.md` | `doc/studio_tooling.md`、`studio/resources/schemas/tool.schema.json`、`studio/resources/tools/` |
| PySide6 Studio、UI、IPC、日志 | `skills/studio/SKILL.md` | `studio/docs/ARCHITECTURE.md`、`studio/docs/TOOL_PLUGINS.md` |
| App、FreeRTOS 任务、业务状态机 | `skills/app/SKILL.md` | `doc/current_architecture.md`、`app/<name>/`、`PROJECT_REQUIREMENTS.md` |
| MaixCamPro、花盆视觉、相机串口协议 | `skills/camera/SKILL.md` | `camera/MaixCamPro/`、`camera/MaixCamPro/flower_pots/protocol.md` |
| 设备 Web 服务（独立仓库，当前为空） | `skills/web/SKILL.md` | 同级 `ark_web/`；当前 SDK 不包含 Web 服务实现 |

跨层任务同时读取所有涉及的 skill。例如“新增带 BH1750 的 I2C 组件”必须同时读取 component 和 hal；“新增 Studio 工具”必须读取 studio 和 tooling。

## 不可违反的架构规则

- `app/<name>/<name>.dts` 是 App 唯一配置源；`status` 缺省或 `okay` 表示启用，`disabled` 节点不得生成、注册或同步到 Keil。
- App、DTS、CubeMX 目录、IOC、Keil 工程和 Target 使用同一个 `<board_prefix>_<product>` 名称。
- CubeMX 生成的 `Core`、`Drivers`、`Middlewares` C/H 只读；不得手改 `ark_dts_generated.c/.h`，使用 `ark_dts.py` 生成。
- 分层方向保持为 App -> 生成 OF/组件 -> `ark_hal_*` -> 平台适配 -> 厂商 HAL。
- 组件/HAL 构建选择分别只来自 `component/common/component_catalog.json` 与 `hal/common/hal_catalog.json`；公共 `component/common/*.c` 按架构始终纳入。
- Studio 是推荐入口，任务成功必须经过产物或日志后置校验，不能只看进程退出码。

## 常用命令

```powershell
python -m studio --check --workspace .
python -m studio --workspace .
python -m studio.cli dts app/c8t6_microcar_soil --check
python -m studio.cli dts app/c8t6_microcar_soil
python -m studio.cli configure app/c8t6_microcar_soil/c8t6_microcar_soil.dts
python -m pytest studio/tests -q
```

优先使用 `python -m studio --workspace .` 启动Studio；CI和自动化通过`python -m studio.cli`调用同一套可导入工具。编译、烧录、清理、克隆和打包属于有副作用操作，执行前必须确认目标工程、探针和输出路径。

## 变更与验证

修改前先检查实际源码、DTS、catalog 和测试。文档或 skill 变更后：

1. 对每个 skill 运行 `python -X utf8 C:\Users\zhous\.codex\skills\.system\skill-creator\scripts\quick_validate.py <skill-dir>`；Windows 中文环境显式使用 UTF-8。
2. 检查引用路径、命令和 JSON 清单。
3. 运行脚本测试及只读 SDK/Studio 检查。
4. 由独立 agent 用真实 HAL、组件、脚本、Studio 和跨层请求验证路由与内容；发现问题只做最小修订并重复验证。

## GitHub 提交流程

此源码仓库在 GitHub 维护，旧 Gitea 历史不导入。首次源码发布使用 main，后续功能修订通过特性分支和 Pull Request。保留原有提交格式与 .githooks/commit-msg，按 README 配置 core.hooksPath。不提交凭据、日志或发布程序，不强推。
