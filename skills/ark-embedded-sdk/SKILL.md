---
name: ark-embedded-sdk
description: 开发、配置、审查和扩展ARK CREW分层嵌入式SDK及其DTS、组件、HAL、App、Studio和Python工具链。
---

# Work on the ARK CREW Embedded SDK

## Project-level routing

先阅读项目根目录 `../../AGENT.md`。本 skill 是兼容入口和全局架构约束；按任务继续读取：

| 任务 | 继续读取 |
|---|---|
| HAL、驱动、DMA、IRQ、CubeMX 绑定 | `../hal/SKILL.md` |
| 组件、DTS OF、传感器、执行器、显示、协议 | `../component/SKILL.md` |
| Python 脚本、工具清单、Keil、Brain | `../tooling/SKILL.md` |
| App、FreeRTOS、业务状态机 | `../app/SKILL.md` |
| MaixCamPro、相机协议 | `../camera/SKILL.md` |
| FastAPI、SQLite、Docker Web 服务 | `../web/SKILL.md` |

跨层修改必须同时读取对应 skill。以下既有章节继续作为架构事实基线，若与源码冲突以源码、DTS、catalog 和测试为准。

## Read the current sources of truth

1. Always read `../../doc/current_architecture.md` completely. It is the authoritative current snapshot.
2. Read `../../doc/ark_sdk_guide.md` for detailed hardware baselines, protocols, component behavior, historical evidence, and troubleshooting.
3. Read `../../app/c8t6_microcar_soil/PROJECT_REQUIREMENTS.md` before changing the maintained product behavior.
4. For script or Studio work, also read `../../doc/studio_tooling.md`, `../../studio/docs/ARCHITECTURE.md`, and `../../studio/docs/TOOL_PLUGINS.md` as relevant.
5. Inspect the actual DTS, generated files, central catalogs, CubeMX `.ioc`, Keil `.uvprojx`, source files, and tests touched by the request. Documentation never overrides current code.

## Preserve the current architecture

- Treat `c8t6_microcar_soil` as the only maintained DTS reference App unless the user explicitly migrates another App.
- Use one canonical name `<board_prefix>_<product>` for the App directory, DTS, CubeMX directory, IOC, Keil project, and Keil Target. Current example: `c8t6_microcar_soil`.
- Use `app/<name>/<name>.dts` as the only App configuration source:
  - `/sys` describes controllers, buses, GPIOs, and component instances.
  - `/software` describes runtime task and product policy.
- Never add App JSON, JSONC, TOML, `/software/build`, machine paths, SDK paths, Keil Target, or Reset-and-Run settings to DTS. Infer them from the canonical name.
- Keep CubeMX-generated `Core`, `Drivers`, and `Middlewares` C/H read-only. CubeMX owns pins, clocks, peripheral parameters, DMA, NVIC, and generated handles.
- Never hand-edit `ark_dts_generated.c/.h`. Generate them with `ark_dts.py`.
- Keep the layer direction:

```text
App logic
  -> generated OF configuration + strongly typed components
    -> ark_hal_* global driver objects / uart_manage
      -> selected platform adapter
        -> CubeMX-generated handles and vendor HAL
```

- Keep product algorithms, scheduling, pages, animation, and state machines in App sources.
- Keep components reusable and limited to device/basic capability behavior.
- Keep HAL limited to normalized hardware operations and platform adaptation.
- Use FreeRTOS native APIs in SDK/App code. Generated CubeMX code may retain CMSIS-RTOS v2.

## Use DTS and central catalogs

- Parse and generate with:

```powershell
python -m studio.cli dts ../../app/<name> --check
python -m studio.cli dts ../../app/<name>
python -m studio.cli configure ../../app/<name>/<name>.dts
```

- Use standard DTS nodes, properties, label/phandle references, `compatible`, `status`, `reg`, `gpios`, `pwms`, and Cell arrays.
- Treat missing `status` or `status = "okay"` as enabled. Disabled nodes must not be generated, registered, or synchronized into Keil.
- Use source-name compatibles such as `oled`, `dht11`, `motor_tb6612`, and `flash_w25q16`; use `ark_hal_*` for HAL controller compatibles.
- Let components query `component/common/ark_dts.c/.h` through `ark_of_*` during registration/init, validate values, and cache typed runtime configuration. Do not perform string-tree searches in real-time paths.
- Maintain all component build metadata only in `component/common/component_catalog.json`:
  - `name` is the compatible and derives `<name>.h`, `<name>_register()`, and `ARK_DTS_HAS_<NAME>`.
  - `sources` is complete.
  - `hal` lists dependencies.
  - Every non-common component C source belongs to exactly one entry.
- Maintain all platforms and HAL drivers only in `hal/common/hal_catalog.json`. Every `hal/<platform>/src/*.c` and `hal/src/*.c` belongs to exactly one driver.
- Never recreate per-component or per-driver JSON manifests and never infer build selection by parsing C source text.

## Implement App and component startup

- Keep the strong App entry named `appStartTask(void *argument)`.
- Load `/software` configuration, call `ark_dts_register_components()`, then run `app_component_boot_run()`.
- Component startup remains one serial task:
  1. Run non-NULL init callbacks serially in init-level order 1, 2, 3.
  2. Run non-NULL self-tests serially.
  3. Report equal-weight progress over `init count + self-test count`.
- Create all App tasks dynamically inside a critical section, then delete the startup task.
- Keep long-running task loops as `while (1)` and periodic tasks on `vTaskDelayUntil()`.
- Do not suppress the UART CLI startup notification because an unrelated optional component
  init or self-test fails. Preserve the failed component state for recovery and diagnostics,
  then let CLI and independent App tasks start when their own initialization succeeded.
- If CubeMX enables IWDG, enable both its `ark_hal_watchdog` DTS controller (with the generated
  `hiwdg` binding) and the `watchdog` component before longer component initialization. A missing
  watchdog component causes repetitive resets that can look like repeated CLI welcomes.
- When a CubeMX-generated startup task has a constrained stack, do not run AT/HTTP or other
  large-buffer component self-tests in it. Start the CLI explicitly after component init, and
  run communication work in a dedicated dynamic task. CLI commands that trigger heavy work
  should enqueue it rather than execute it on the CLI task stack.
- Define reusable components as `ark_component_t` objects with name, optional init, optional self-test, expected duration, and init level.
- Expose no-argument `<name>_register(void)` functions. Registration only attaches the component object to the global bus.
- Keep simple components flat. Component classes may group a core and related backends under `component/<class>/`, with catalog `directory` selecting the shared directory.
- Prefer strongly typed component APIs. Do not reintroduce the removed generic `common_dev` string `/dev` registry. Add an abstraction only for a real shared capability, such as `ark_stream`.

## Implement HAL and UART/stream consumers

- Name public HAL contracts and types `ark_hal_*` under `hal/include`.
- Declare and define one global driver object such as `ark_hal_gpio`; do not add runtime platform registration/getters.
- Bind adapters through generated `ark_hal_bindings.h` tables; never duplicate CubeMX handles, MSP init, DMA handles, or IRQ handlers.
- Route UART ownership, DMA-to-idle receive, callbacks, and IRQ observers through `ark_uart_manage`.
- When DMA channel conflicts make UART RX DMA unavailable, use the normalized
  `ARK_UART_RX_INTERRUPT` mode with a one-byte HAL interrupt receive and a bounded
  component-side FreeRTOS stream/ring buffer. Keep ISR work limited to buffering
  and task notification; do not move the conflict into CubeMX-generated code.
- Keep CLI/protocol code transport-independent through `ark_stream_read()` and `ark_stream_write()`.
- Use only FreeRTOS `...FromISR` APIs in interrupt callbacks and never print from an ISR.
- For a DHT11 node, describe the data line with `data-gpios`; the component owns the
  open-drain/output-to-input timing transition. App code should publish only checksum-valid
  samples and must not report old server-side simulated telemetry as a board measurement.

## Maintain Studio and scripts

- Use `python ../../studio/studio.py --dev` as the supported desktop entry.
- Discover Apps only from `app/<name>/<name>.dts`. Changing the Studio App selector must not modify Keil; require explicit `keil.sync`.
- Discover formal tools from `../../studio/resources/tools/*.json`.
- Show every formal tool in the workbench tool center. Use `quick` only for the optional top-menu shortcut list; never use it to hide tool cards.
- Create a new unified App with the Studio `project.create_app` handler or `python -m studio.cli app-create`. It must transactionally clone a same-platform CubeMX template, create the same-named minimal DTS/App, generate OF C/H, synchronize the new Keil copy, and roll back only the newly created directories on failure.
- Every formal tool must be active and declare:
  - `parameter_mode: none|fixed|dynamic`; none has no inputs, fixed persists per workspace/App/tool, and dynamic opens a top-down overlay on the current route. Dynamic inputs persist only when they declare `remember: true`;
  - a real, bounded `check` handler;
  - `event` or `query` progress;
  - resource locks and cancellation semantics where relevant;
  - post-validation for material outputs.
- Keep progress truthful. UI shimmer/rings show activity only; percent changes only from real events, queries, or successful completion validation.
- For Keil builds, pre-count full or dependency-stale compile units and advance from actual `compiling/assembling` output.
- For flash, estimate only between real erase/program/verify stages using firmware size, target Flash, and probe rate; require `Verify OK` and `Application running ...` for success.
- Probe check may actively read SWD DPIDR/CPU through short-timeout OpenOCD, but must not reset, halt, erase, program, or download.
- Resolve `UV4.exe` from SDK environment inspection; do not add manual Keil path fields to tools.
- Keep Brain settings, remembered parameters, history, and structured logs under `%LOCALAPPDATA%/ARKCrewStudio`.
- Execute Studio tools through imported builtin handlers. External Keil, OpenOCD, DTC and shell processes must use the hidden process runner with no inherited console; user-requested desktop applications such as VS Code and Explorer may remain visible.
- Keep the serial monitor inside Studio as one resource-locked session. The terminal sends printable keys, ANSI navigation sequences and control bytes through `serial.write(dataHex)`, reserves `Ctrl+Shift+C/V` for local copy/paste, and defaults local echo off.
- When baud is not explicitly selected, resolve the App's `uart_cli -> stream -> current-speed` DTS path; never silently assume 115200 for a CLI App.
- Auto-dismiss transient global error banners after five seconds while preserving task failure details in task history.
- Keep new SDK text files UTF-8. The `sdk.encoding.convert` Studio tool requires a selected directory and may convert
  only supported source/config suffixes directly; it must preserve
  original bytes under LocalAppData and never rewrite binary or unknown-encoded files.
- Web control credentials stay only in the server .env. Do not embed WEB_CONTROL_KEY
  in static assets, DTS, source, logs, or user-facing documentation; clear stale
  browser session keys and prompt again after a 401 response.
- For c8t6_ark_net use newline-delimited JSON over persistent TCP, not per-request HTTP. Register every device in the dashboard with its DTS ark,device-password; never expose that password in responses or logs. Send a heartbeat every 5 seconds and treat 30 seconds without a heartbeat as offline. The LED control must be a disabled gray switch while offline.
- The ARK web console owns its own SQLite database and must not reuse another server application's database. Maintain users, sessions, devices, telemetry, and activity tables; store session tokens as digests and device/user secrets as salted PBKDF2 hashes. Preserve and migrate the existing data volume during deployment. Protect all dashboard device APIs with HTTP-only login sessions. The initial admin account is admin; set ADMIN_PASSWORD in the deployment environment. This web credential is distinct from every DTS device password.
- Keep the web console source UTF-8 and serve static assets with no-store caching. Its current UI consists of a responsive device overview, per-device telemetry trend and LED control, and an activity log. Preserve request timeouts, duplicate-submit protection, session-expiry recovery, 5-second live refresh, 30-second offline state, and the disabled LED switch while a device is offline.
- Preserve the current short fade-only route/modal transitions and VS Code Windows font strategy unless the user asks for another visual direction.

## Pause for user-owned external actions

- Pause when completion requires a physical reset, cable/probe/USB connection, power cycle, wiring change, CubeMX UI edit, or regeneration the agent cannot safely perform.
- Tell the user exactly what to do, why, and what generated file, log, register, or hardware behavior will prove completion.
- After confirmation, re-inspect the changed `.ioc`, generated sources, device state, and Keil metadata, then resume without repeating completed work.
- Do not bypass a required CubeMX action by hand-editing generated hardware code.
- For ESP-01 on STM32F1 without RX DMA, set USART3 NVIC priority to 4/0 in
  CubeMX and regenerate. Its ISR may only write the component-owned static
  ring buffer; it must not call FreeRTOS FromISR APIs at priority 4.

## Validate proportionally

For DTS/catalog changes:

```powershell
python -m studio.cli dts ../../app/c8t6_microcar_soil --check
python -m studio.cli project audit-paths ../../..
```

For firmware changes, generate/synchronize then run a Keil full rebuild. Require 0 errors, 0 warnings, AXF/HEX existence, and record Flash/SRAM. Do not flash unless requested.


After updating this skill, validate it with skill-creator:

```powershell
python C:\Users\zhous\.codex\skills\.system\skill-creator\scripts\quick_validate.py ../../skills/ark-embedded-sdk
```

## Keep documentation synchronized

After any architectural, configuration, App, component, HAL, script, Studio, or workflow change, update:

1. `../../doc/current_architecture.md`
2. `../../doc/ark_sdk_guide.md`
3. `../../doc/studio_tooling.md` when tooling changes
4. this `SKILL.md`

Keep current facts in `current_architecture.md`. Mark old Apps and historical measurements explicitly as legacy rather than presenting them as current behavior.
