---
name: ark-sdk-app
description: Develop or review ARK CREW SDK Apps, DTS configuration, FreeRTOS tasks, component boot, and product state machines.
---

# App 开发 Skill

## 先读什么

- `../../doc/current_architecture.md`
- 当前 App 的 `PROJECT_REQUIREMENTS.md`（若存在）
- `app/<name>/<name>.dts`、`include/`、`src/` 和生成的 `ark_dts_generated.*`
- 涉及硬件时再读 `../hal/SKILL.md`；涉及器件时再读 `../component/SKILL.md`

## App 约束

- 维护 App 使用 `app/<board_prefix>_<product>/`，DTS、CubeMX、IOC、Keil 工程和 Target 同名。
- DTS 分 `/sys` 和 `/software`：前者声明控制器/组件，后者保存任务周期、优先级和产品策略。
- 入口保持强定义 `appStartTask(void *argument)`。启动顺序为读取软件配置、注册组件、按 init level 初始化、自检、创建业务任务，再删除启动任务。
- 业务算法、显示页面、动画和状态机放在 App；可复用器件能力放在 component；硬件归一化操作放在 HAL。
- 生成文件只由 `ark_dts.py` 更新。CubeMX 生成目录保持只读。

## 任务与运行时

使用 FreeRTOS 原生 API；任务周期和策略参数优先来自 `/software`。采用快照把采集与显示解耦，避免 OLED/LCD 任务直接驱动阻塞式传感器操作。长耗时舵机、视觉或探针流程必须处理超时、取消和状态回收，不能用长时间全局临界区暂停系统。

## 验证

```powershell
python -m studio.cli dts app/<name> --check
python -m studio.cli dts app/<name>
python -m studio.cli configure app/<name>/<name>.dts --dry-run
python -m pytest studio/tests -q
```

修改 maintained App 行为时，补充或更新对应 App 测试/需求记录，并检查 `ARK_DTS_HAS_*` 条件编译和禁用节点没有意外启用。
