# MaixPy API 说明

参考官方文档：

- [文档首页](https://wiki.sipeed.com/maixpy/doc/zh/index.html)
- [相机](https://wiki.sipeed.com/maixpy/doc/zh/vision/camera.html)
- [显示](https://wiki.sipeed.com/maixpy/doc/zh/vision/display.html)
- [色块检测](https://wiki.sipeed.com/maixpy/doc/zh/vision/find_blobs.html)
- [图像 API](https://wiki.sipeed.com/maixpy/api/maix/image.html)
- [串口](https://wiki.sipeed.com/maixpy/doc/zh/peripheral/uart.html)
- [触摸](https://wiki.sipeed.com/maixpy/doc/zh/vision/touchscreen.html)
- [触摸 API](https://wiki.sipeed.com/maixpy/api/maix/touchscreen.html)
- [线程 API](https://wiki.sipeed.com/maixpy/api/maix/thread.html)

## 相机与应用循环

```python
from maix import app, camera

cam = camera.Camera(width=480, height=320, buff_num=2)
while not app.need_exit():
    frame = cam.read()
```

使用 app.need_exit 保证从 MaixVision/App 环境正常退出。buff_num=2 让采集和处理重叠，但增加一帧缓存延迟。当前组件采集 480×320 并使用相同原始坐标；实际速度通过 time.fps 测量，不强制不支持的 FPS/分辨率组合。

相机尺寸不限于固定枚举，官方接口接受偶数宽高，GC4653/OS04A10 传感器最高 2560×1440。224×224、320×320 用于方形 AI 输入，320×224 为低成本宽画面，320×240 为 QVGA。480×320 在本检测器测得约 48 FPS；480×360 或 640×480 适合 4:3 显示，像素越多算法越慢。1280×720 传感器模式支持 60/80 FPS，但 Python 算法通常更慢；2560×1440 的 30 FPS 模式不适合本全帧色块循环。使用 camera.get_sensor_size 与 Camera/Display 的 width/height 查询真实尺寸。

## LAB 色块检测

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

阈值顺序为 L_MIN、L_MAX、A_MIN、A_MAX、B_MIN、B_MAX。官方绿色示例是 [0,80,-120,-10,0,30]；浅绿色需要提高亮度下限并现场标定。x_stride/y_stride 控制采样步长，增大会遗漏细小目标，当前使用 2/1。area_threshold 与 pixels_threshold 过滤完成检测的色块，不跳过全帧 LAB 扫描。merge 合并扩展矩形相交的色块，margin 控制扩展范围。索引 0..3 分别是 x、y、宽、高，按宽×高选择最大目标。

## 绘制与显示

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

draw_circle 可逐帧缩小半径作为采样动画；自定义标记可使用 draw_line。用 time.time 判断动画时长，time.fps_start/time.fps 测量滚动帧率，显示四舍五入后的整数 FPS。

## 触摸与线程

```python
from maix import app, thread, time, touchscreen

def touch_worker(_args):
    ts = touchscreen.TouchScreen()
    while not app.need_exit():
        x, y, pressed = ts.read()
        time.sleep_ms(1)

thread.Thread(touch_worker).detach()
```

read 返回 x、y、pressed，通过按下后释放判定点击。降低 CPU 消耗可使用 available(timeout) 后 read0。显示采用 FIT_CONTAIN，必须将屏幕触摸坐标反向映射到图像坐标：

```python
frame_x, frame_y = image.resize_map_pos_reverse(
    frame.width(), frame.height(), disp.width(), disp.height(),
    image.Fit.FIT_CONTAIN, touch_x, touch_y,
)
```

Thread 构造的可选第二参数是 C 扩展 capsule，不是任意 Python 对象。共享 Python 状态保存在模块层并加锁，启动时不传 args。

官方 C++ 示例 [app_find_blobs](https://github.com/sipeed/MaixCDK/tree/main/projects/app_find_blobs) 使用 2/1 步长及原生尺寸，可作为 Python 性能上限参考。相机循环不直接 serial.write；将最新完整包放入加锁状态，由独立发送线程发送，替换旧待发送包以避免串口背压拖慢画面。

## 二进制协议

```python
from maix import err, pinmap, uart

from maix.v1.machine import UART
serial = UART("/dev/ttyS0", 115200)
```

小于号表示小端，B 是无符号字节，H 是无符号 16 位整数。命令紧跟长度；命令 1 花盆帧为 14 字节，命令 2 至 5 的空请求为 5 字节，STM32 响应追加状态与可选标定数组。

## 性能诊断与 OpenCV

PROFILE_DEBUG 开启时每秒打印 MAIX_PROFILE。read_ms、find_ms、draw_ms、show_ms 分别表示采集、检测、Python 绘制和显示提交。camera_fps 是驱动帧率，loop_fps 是循环实测；show_call_ms 是单次提交，show_ms 是分摊到检测帧的耗时。MaixVision 会额外压缩上传图像，正式测速应脱离 MaixVision 并关闭分析输出。

[OpenCV 官方说明](https://wiki.sipeed.com/maixpy/doc/zh/vision/opencv.html)：image.image2cv 的 ensure_bgr=False、copy=False 可共享 NumPy 缓冲区，使用期间原 Image 必须存活。相机提供 BGR888 可避免颜色转换；优先使用检测分辨率的相机/ISP 通道，不每帧 resize。只有启用独立全分辨率显示流时才加相机通道。OpenCV LAB 是无符号 8 位：L_cv=round(L×255/100)，a_cv=a+128，b_cv=b+128。
