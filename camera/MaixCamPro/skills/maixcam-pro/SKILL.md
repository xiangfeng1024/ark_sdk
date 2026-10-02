---
name: maixcam-pro
description: Develop and maintain MaixCAM Pro MaixPy camera, image blob detection, display annotation, UART framing, and STM32 interoperability code in ark_sdk/camera/MaixCamPro. Use for MaixPy API questions, green-target calibration, camera-side scripts, or the 0x5A 0x5B protocol.
---

# MaixCam Pro

## Workflow

1. Read `references/maixpy-api.md` before changing MaixPy calls or pin mappings.
2. Send geometry in the capture frame's raw coordinate system. The packet has no fixed resolution; keep camera-side UI dimensions synchronized with the selected capture size.
3. Use `camera.Camera(..., buff_num=2)` so capture can overlap processing. Do not force unsupported high-rate geometry combinations during camera open.
4. Use LAB thresholds and calibrate them with the real target and lighting.
5. Use the official `x_stride=2`, `y_stride=1` baseline. At 480x320 use `area_threshold=500`, `pixels_threshold=500`, and `margin=8`; these result filters do not avoid the full-frame threshold scan.
6. Merge neighboring blobs with `merge=True` and a deliberate `margin`, then select the largest merged rectangle.
7. Run detection, UART publication, drawing, and `disp.show` every frame in the installed application. MaixVision mode also compresses and sends each shown image to the workstation, so profile production FPS by running the packaged app outside MaixVision.
8. Keep the on-screen target annotation minimal: bounding box, center marker, and center coordinates only. Do not draw LAB thresholds or target width/height.
9. Encode binary UART fields explicitly with little-endian `struct.pack`.
10. Publish each complete packet to the latest-frame slot; a detached UART worker owns `serial.write` so camera capture never waits on UART.
11. Keep `flower_pots/protocol.md`, `flower_pots/maixpy/main.py`, and the STM32 parser byte-for-byte consistent.
12. Compile STM32 changes without flashing unless hardware testing is explicitly requested.

## OpenCV Comparison Version

- Use `flower_pots/opencv/main.py` as the OpenCV comparison. The production entrypoint is `flower_pots/maixpy/main.py`; preserve its packaging metadata and dist output.
- Capture `FMT_BGR888`, then call `image.image2cv(..., ensure_bgr=False, copy=False)` so OpenCV receives the camera buffer without a color conversion or memory copy.
- Request the downscaled BGR detection channel directly from the camera ISP; do not spend CPU time on `cv2.resize`. Restore rectangles, centers, and UART geometry to the 480x320 coordinate system.
- Convert saved OpenMV-style LAB thresholds to OpenCV's 8-bit LAB representation before calling `cv2.inRange`.
- Merge nearby mask regions with a closing operation and use `connectedComponentsWithStats` to filter and select the largest region.
- Use `MAIX_CV_PROFILE` stage timings to compare resize, LAB conversion, masking, morphology, connected components, drawing, and display costs independently.
- Keep the comparison script self-contained. Leave `DISPLAY_ENABLED = False` for isolated recognition profiling; this skips drawing and `disp.show()` while continuing camera reads, detection, UART publication, and profile printing.
- Set OpenCV to one worker thread on MaixCAM Pro before profiling; its default worker scheduling can cost more than the small 240x160 operations.
- OpenCV runs on the CPU. Treat it as a measured alternative, not an assumed optimization over MaixPy's native image implementation.

## Touch Calibration

- Use `maix.touchscreen.TouchScreen.read()` in a detached `maix.thread.Thread`; keep camera capture and display in the main loop.
- During the first10 seconds, show a high-contrast black panel with a yellow border and large seven-segment countdown plus the `进入循迹校准` button. After stabilization, stay on the start page and prompt for `启动任务`; flash the button yellow, queue command6, then enter normal vision. Exiting tracking calibration returns to this start page and never starts the vehicle implicitly.
- In tracking calibration mode, submit a black canvas rather than the camera frame. Separate Flash/default current values from optional sampled values; require both samples before Flash write. Provide five24-pixel Chinese buttons including exit, with short yellow press feedback.
- Tracking calibration is additive. Normal vision mode must retain point-based LAB threshold sampling, the shrinking white-circle feedback, SAVE persistence, and the post-save touch shutdown behavior.
- Use `TouchScreen.available(0)` followed by `read0()` and an explicit 20 ms sleep. Continuous `available(5)` calls contend for the Python GIL on the tested firmware and reduce vision performance from about 60 FPS to 35 FPS.
- Ignore the first 2000 ms of touch input by delaying `TouchScreen` construction, preventing launcher or startup touches from becoming calibration events.
- Treat a release inside the save button as a save request. Treat any other release in the 480x320 image as a calibration sample point.
- Convert physical touchscreen coordinates to the 480x320 frame with `image.resize_map_pos_reverse(..., image.Fit.FIT_CONTAIN, ...)` before hit-testing or sampling.
- Read the RGB sample with `frame.get_pixel(x, y, rgbtuple=True)`, convert it to LAB, and clamp each channel to its legal range after applying +/-20.
- Load the JSON threshold file at startup and write it only in response to the save request. Keep the file path and schema documented.
- When save and sample events arrive together, prioritize save and discard the sample event for that frame.
- After a successful save, disable touch event reads, let the touch worker exit, and stop drawing the SAVE control. Keep only the temporary save-success notice.
- Show a short white shrinking circle at the sampled point, and show a temporary green save-success notice after the JSON write completes.
- Measure displayed capture FPS over a time window and render a rounded integer in yellow at the top-right of the frame.

## Component Rules

- This project uses the MaixCAM Pro screen-side connector, matching the verified reference implementation: construct `maix.v1.machine.UART("/dev/ttyS0", 115200)` and do not configure A19/A18 pinmap. Use `/dev/ttyS1` with A19/A18 only when the hardware is wired to that separate header.
- The current `microcar_soil` receiver is STM32F103 USART2 PA3 at 115200 with DMA-to-idle; keep Beidou disabled while MaixCam owns USART2.
- Keep UART writes in a detached worker. The shared slot intentionally drops stale packets and retains only the newest complete frame packet.
- The UART worker must also read STM32 responses and parse variable-length `5A 5B len command payload A5` frames. Prioritize queued calibration and task-start requests over replaceable flower frames. Command6 has an empty request payload and starts STM32 business logic only when it is stopped or faulted.
- Treat command7 as an asynchronous STM32 task-ready notification that may arrive before normal vision starts. Latch it until command6 receives a successful response; never consume and discard it based on the current page. While latched in normal vision, show a120x42 `启动任务` button below FPS and re-enable/recreate the touch worker if SAVE previously stopped it. STM32 broadcasts command7 once per second while stopped or faulted, and pressing the overlay queues command6.
- Send a target-loss packet instead of becoming silent when no blob is found.
- Treat the bundled shallow-green threshold as a starting point, not a universal calibration.
- Do not add a checksum unless both camera and STM32 protocol versions are updated together.
- Preserve byte-stream resynchronization and validate decoded geometry without assuming a fixed frame resolution.
- The official native reference is `sipeed/MaixCDK/projects/app_find_blobs`; it uses a display-sized camera, native C++ processing, `x_stride=2`, `y_stride=1`, and no Python-side per-frame work. Use it as a performance baseline.

## Performance Diagnostics

- Set `PROFILE_DEBUG = True` to print one `MAIX_PROFILE` line per second; this is low frequency but still adds small timing overhead.
- Use480x320 for both versions under `flower_pots`; the measured native detector rate is about48 FPS with display and touch enabled.
- Print `MAIX_VIDEO` once at startup with capture, sensor, and display dimensions. Use the reported display aspect ratio to explain letterboxing under `FIT_CONTAIN`.
- Compare `read_ms`, `find_ms`, `draw_ms`, and `show_ms` against `total_ms` to locate the dominant stage. `camera_fps` is the camera driver's rate, while `loop_fps` and `display_fps` describe the Python loop.
- `uart_replace` counts complete packets replaced before the sender took them; it should remain near zero when UART keeps up.
- `display_calls` and `display_rate` report actual display submissions. `show_call_ms` is the cost of one real `disp.show` call.
- Set `PROFILE_DEBUG = False` after collecting the log before measuring the optimized production frame rate.

## References

Read `references/maixpy-api.md` for common camera, image, display, UART, pinmap, application-loop, and packet APIs with official documentation links.
