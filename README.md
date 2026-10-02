# ark_sdk

ARK CREW embedded SDK source repository. This GitHub repository starts with a new source snapshot; no legacy Gitea history is imported.

## ARK CREW Studio

默认桌面工作台使用 PySide6，首次启动可选择任意 ARK CREW SDK 工作区：

```powershell
python -m pip install -r studio/requirements.txt
python -m studio --check --workspace .
python -m studio --workspace .
python -m studio --build
```

Windows 发布包输出到 `studio/dist/ARKCrewStudio-0.3.0-windows-x64/`。Studio、Brain、
工具清单和SDK Python工具链均位于`studio/`，使用统一的PySide6与Python运行环境。

## 提交规范

提交消息使用以下结构：

```text
Type: <类型>

Module: <模块路径>

<变更内容>

Change-Id: I<40 位十六进制字符>
Signed-off-by: <姓名> <邮箱>
```

仓库已提供 `.githooks/commit-msg`，会在提交时自动生成 `Change-Id`。首次克隆后执行：

```bash
git config core.hooksPath .githooks
```

`Change-Id` 是便于检索和关联修订的提交尾注；Git 本身还会为每个提交自动生成不可变的提交对象 ID（commit SHA），可用 `git rev-parse HEAD` 查看。

## Workspace layout

Clone the SDK and STM32 repositories as siblings. No symbolic links are required:

```text
workspace/
  ark_sdk/
  ark_stm32_projects/
```

The two maintained DTS Apps are c8t6_microcar_soil and c8t6_ark_net (the latter maps to the existing c8t6_xiaoyan_net board folder). Other JSON-based Apps are retained as historical examples. Use the maintained DTS Apps for the current Studio configuration workflow.

```powershell
python -m pytest studio/tests -q
python -m studio.cli dts app/c8t6_microcar_soil --check
python -m studio.cli dts app/c8t6_ark_net --check
python -m studio --check --workspace .
```

Documentation, component guides, generator templates and SDK schemas are consolidated under doc/. studio/docs retains application-specific architecture and UI documentation. Build/distribution output, caches and logs are excluded; fonts, icons, schemas, licenses, hand-maintained PyInstaller spec and DTS-generated build inputs are retained.

The network DTS contains example placeholders for Wi-Fi and device passwords. Set local configuration before connecting to a real network, and regenerate DTS output using the CLI; do not commit real credentials. The existing LICENSE.txt and third-party notices remain in force.

Detailed clean-source tests and the existing firmware build limitations are documented in [doc/SOURCE-VERIFICATION.md](doc/SOURCE-VERIFICATION.md).
