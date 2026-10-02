# MaixPy API Notes

Official documentation studied for this component:

- Index: https://wiki.sipeed.com/maixpy/doc/zh/index.html
- Camera: https://wiki.sipeed.com/maixpy/doc/zh/vision/camera.html
- Display: https://wiki.sipeed.com/maixpy/doc/zh/vision/display.html
- Blob detection: https://wiki.sipeed.com/maixpy/doc/zh/vision/find_blobs.html
- Image API: https://wiki.sipeed.com/maixpy/api/maix/image.html
- UART: https://wiki.sipeed.com/maixpy/doc/zh/peripheral/uart.html
- Touchscreen: https://wiki.sipeed.com/maixpy/doc/zh/vision/touchscreen.html
- Touchscreen API: https://wiki.sipeed.com/maixpy/api/maix/touchscreen.html
- Thread API: https://wiki.sipeed.com/maixpy/api/maix/thread.html

## Camera and Application Loop

```python
from maix import app, camera

cam = camera.Camera(width=480, height=320, buff_num=2)
while not app.need_exit():
    frame = cam.read()
```

Use `app.need_exit()` so the program exits cleanly from the MaixVision/App environment. `buff_num=2` allows capture and processing to overlap at the cost of one additional buffered frame of latency. The component captures at 480x320 and sends geometry in the same raw coordinate system. Do not force an unsupported camera FPS/geometry combination. The actual loop rate should be measured with `time.fps()`.

Camera dimensions are not limited to a small fixed enum. The official API accepts even width and height values and supports up to 2560x1440 on GC4653/OS04A10 sensors. Practical tiers are:

- `224x224` or `320x320`: square AI model input; side letterboxing on a wide display.
- `320x224`: low-cost wide AI input.
- `320x240`: QVGA 4:3, fast general-purpose vision.
- `480x320`: custom 3:2, measured near 48 FPS in this detector.
- `480x360` or `640x480`: 4:3 choices for a 4:3 display; more pixels reduce algorithm FPS.
- `1280x720`: 16:9 HD, sensor mode supports 60/80 FPS but Python algorithms usually run much slower.
- `2560x1440`: native 16:9 high-quality mode at 30 FPS; unsuitable for this full-frame Python blob loop.

Use `camera.get_sensor_size()` for the attached sensor's native size. `Camera.width()`, `Camera.height()`, `Display.width()`, and `Display.height()` report actual runtime dimensions.

## LAB Blob Detection

```python
blobs = frame.find_blobs(
    thresholds,
    x_stride=2,
    y_stride=1,
    area_threshold=500,
    pixels_threshold=500,
    merge=True,
    margin=8,
)
```

Each RGB threshold is `[L_MIN, L_MAX, A_MIN, A_MAX, B_MIN, B_MAX]`. The official green example is `[0, 80, -120, -10, 0, 30]`. For a light-green target, raise the lower L bound and calibrate all limits on the real scene.

`x_stride` and `y_stride` control the scan sampling interval. Larger values
reduce sampled points but can miss thin or small targets. The current component
uses the official `2/1` baseline. `area_threshold` filters completed blob
bounding areas and `pixels_threshold` filters completed blobs with too few
matching pixels; increasing these thresholds does not skip the full-frame LAB
threshold scan. `merge=True` merges blobs whose expanded bounding rectangles
intersect. `margin` expands the intersection test, allowing nearby fragments
to merge. Blob indexes 0..3 are `x`, `y`, `width`, and `height`. Select the
largest merged target with `max(blobs, key=lambda blob: blob[2] * blob[3])`.

## Drawing and Display

```python
from maix import display, image

disp = display.Display()
color = image.Color.from_rgb(255, 0, 0)
frame.draw_rect(x, y, width, height, color, thickness=2)
frame.draw_cross(center_x, center_y, color, size=10, thickness=2)
frame.draw_circle(sample_x, sample_y, radius, image.Color.from_rgb(255, 255, 255), thickness=2)
frame.draw_string(x, y, "target", color=color)
disp.show(frame, fit=image.Fit.FIT_CONTAIN)
```

`draw_circle` can be redrawn each frame with a decreasing radius to provide a short sample-point animation. `draw_line(x1, y1, x2, y2, color, thickness=1)` is available when a custom marker is needed.

Use `time.time()` for elapsed-time measurements such as animation expiry and
use `time.fps_start()` followed by `time.fps()` for the official rolling FPS
measurement. Display FPS as an integer after rounding the measured frame rate.

## Touchscreen and Thread

```python
from maix import app, thread, time, touchscreen

def touch_worker(_args):
    ts = touchscreen.TouchScreen()
    while not app.need_exit():
        x, y, pressed = ts.read()
        time.sleep_ms(1)

thread.Thread(touch_worker).detach()
```

`TouchScreen.read()` returns `(x, y, pressed)`. Detect a click by recording a pressed state and handling the following release. For lower CPU use, call `available(timeout)` and then `read0()` so the worker waits for an event instead of polling continuously.

The component captures 480x320. Query `Display.width()` and `Display.height()` at runtime instead of assuming the physical output size.
`disp.show(frame, fit=image.Fit.FIT_CONTAIN)` scales the frame and adds
letterbox space. Convert a display-space touch point back to frame coordinates
before using it:

```python
frame_x, frame_y = image.resize_map_pos_reverse(
    frame.width(), frame.height(), disp.width(), disp.height(),
    image.Fit.FIT_CONTAIN, touch_x, touch_y,
)
```

`Thread.__init__` accepts a Maix C-extension `capsule` as its optional second
argument, not an arbitrary Python object. When a worker needs shared Python
state, keep that state at module scope (protected by a lock) and start the
worker without an `args` value, as shown above.

For RGB888 frames, `frame.get_pixel(x, y, rgbtuple=True)` returns `[R, G, B]`. `find_blobs` still expects LAB thresholds, so convert a sampled RGB value to LAB before constructing a threshold.

The component stores its threshold JSON at `/root/maixcam_threshold.json` with the schema `{"thresholds": [[L_MIN, L_MAX, A_MIN, A_MAX, B_MIN, B_MAX]]}`. Validate ranges before using values loaded from disk.

## UART

```python
from maix import err, pinmap, uart

from maix.v1.machine import UART
serial = UART("/dev/ttyS0", 115200)
```

This project uses the screen-side connector and follows the board-proven `/dev/ttyS0` `maix.v1.machine.UART` implementation without pinmap calls. The separate A19/A18 header uses UART1 and `/dev/ttyS1`; select it only when wired to those pins.

The official native color-block application is available at
`https://github.com/sipeed/MaixCDK/tree/main/projects/app_find_blobs`.
Its C++ loop uses `x_stride=2`, `y_stride=1`, and native camera/display
dimensions; this explains why it is a useful upper-bound performance reference
for the Python implementation.

The camera loop must not call `serial.write` directly. Store the newest complete
packet under a lock, and let a detached worker take the packet and call
`serial.write`. Replacing an older pending packet prevents UART backpressure
from delaying capture and display.

## Binary Packet Encoding

```python
from struct import pack

packet = pack("<BBBBBHHHHB", 0x5A, 0x5B, 14, 1, flags, cx, cy, width, height, 0xA5)
serial_state.publish(packet)
```

`<` selects little-endian, `B` is one unsigned byte, and `H` is one unsigned 16-bit value. The command byte follows `len`; command1 flower frames are14 bytes. Commands2 through5 use5-byte empty requests, while STM32 responses add a status byte and optional calibration arrays.

## Performance Diagnostics

With `PROFILE_DEBUG = True`, the component prints one `MAIX_PROFILE` line per
second. `read_ms` measures camera capture, `find_ms` measures `find_blobs`,
`draw_ms` measures Python annotation work, and `show_ms` measures display
submission. `camera_fps` is the camera driver's rate; `loop_fps` is the measured
Python loop rate. `show_call_ms` is the cost of one display submission, while
`show_ms` is that cost amortized across all detection frames. In MaixVision
mode, `Display.show` also compresses and sends the image to the workstation;
measure production FPS by running the installed package outside MaixVision.
Disable profiling before the final FPS measurement.
# OpenCV interop

- Official guide: `https://wiki.sipeed.com/maixpy/doc/zh/vision/opencv.html`
- `image.image2cv(img, ensure_bgr=False, copy=False)` exposes a compatible camera buffer as a NumPy array without copying. The source `Image` must remain alive while OpenCV uses the array.
- Request `image.Format.FMT_BGR888` from the camera when OpenCV will consume the frame; this avoids an RGB-to-BGR conversion before OpenCV processing.
- Prefer a camera/ISP channel at the detection resolution over `cv2.resize` in every loop. Use `Camera.add_channel` only when a separate full-resolution display stream is enabled.
- OpenCV LAB channels are unsigned 8-bit values. Convert Maix/OpenMV thresholds with `L_cv = round(L * 255 / 100)`, `a_cv = a + 128`, and `b_cv = b + 128`.
