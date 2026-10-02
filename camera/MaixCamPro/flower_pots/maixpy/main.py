# /**
#  * SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
#  * SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0
#  *
#  * ARK CREW LIMITED NON-COMMERCIAL LICENSE NOTICE
#  *
#  * This source code, together with its associated documentation, examples,
#  * configuration files, and related materials, is collectively referred to
#  * as the "Software".
#  *
#  * Subject to the complete terms set forth in the LICENSE file, Ark Crew
#  * grants you a limited, non-exclusive, non-transferable, and non-sublicensable
#  * right to access, reproduce, and modify the Software solely for personal
#  * study, classroom education, academic research, and non-commercial evaluation.
#  *
#  * Commercial use of the Software, in whole or in part, is strictly prohibited
#  * without prior written authorization from Ark Crew. Prohibited activities
#  * include, without limitation, sale, sublicensing, paid distribution, use in
#  * paid consulting or training, incorporation into any commercial product or
#  * service, and internal development intended for commercial deployment.
#  *
#  * Except for the limited rights expressly granted under the applicable
#  * License, no license or other right, whether express, implied, by estoppel,
#  * or otherwise, is granted under any copyright, patent, trademark, trade
#  * secret, mask work, or other intellectual property right belonging to
#  * Ark Crew or any third party.
#  *
#  * Delivery or disclosure of the Software does not convey permission to use
#  * the Ark Crew name, trademarks, logos, visual identity, or other branding,
#  * except where strictly necessary to preserve the original attribution.
#  *
#  * THE SOFTWARE IS PROVIDED "AS IS" AND "WITH ALL FAULTS", WITHOUT ANY
#  * REPRESENTATION OR WARRANTY OF ANY KIND, WHETHER EXPRESS, IMPLIED,
#  * STATUTORY, OR OTHERWISE, INCLUDING WARRANTIES OF MERCHANTABILITY,
#  * FITNESS FOR A PARTICULAR PURPOSE, TITLE, ACCURACY, RELIABILITY, AND
#  * NON-INFRINGEMENT.
#  *
#  * TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, ARK CREW SHALL NOT
#  * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
#  * PUNITIVE, OR CONSEQUENTIAL LOSS OR DAMAGE ARISING FROM OR RELATED TO
#  * THE SOFTWARE, ITS USE, OR ITS INABILITY TO BE USED.
#  *
#  * This notice shall be retained in all authorized copies or substantial
#  * portions of the Software. Removal, concealment, or unauthorized alteration
#  * of this notice is prohibited.
#  *
#  * See the LICENSE file in the root directory of this repository for the
#  * complete and controlling license terms.
#  */

import json
from struct import pack, unpack
from threading import Lock

from maix import app, camera, display, image, thread, time
from maix import touchscreen
from maix.v1.machine import UART


FRAME_WIDTH = 480
FRAME_HEIGHT = 320
UART_DEVICE = "/dev/ttyS0"
UART_BAUD_RATE = 115200
PACKET_SIZE = 14
COMMAND_FLOWER = 1
COMMAND_CAL_WHITE = 2
COMMAND_CAL_BLACK = 3
COMMAND_FLASH_WRITE = 4
COMMAND_FLASH_READ = 5
COMMAND_START_TASK = 6
COMMAND_TASK_READY = 7
STATUS_DEFAULTS = 1
STATUS_ERROR = 2
STARTUP_STABLE_SECONDS = 10.0
THRESHOLD_PATH = "/root/maixcam_threshold.json"
THRESHOLD_DELTA = 20
DEFAULT_THRESHOLDS = [[45, 100, -80, -5, -10, 60]]
BLOB_X_STRIDE = 2
BLOB_Y_STRIDE = 1
PIXELS_THRESHOLD = 500
AREA_THRESHOLD = 500
MERGE_MARGIN = 8
CAMERA_BUFFER_COUNT = 2
DISPLAY_FRAME_DIVIDER = 1
MARK_COLOR = image.Color.from_rgb(255, 0, 0)
UI_COLOR = image.Color.from_rgb(255, 255, 255)
FPS_COLOR = image.Color.from_rgb(255, 255, 0)
SAMPLE_COLOR = image.Color.from_rgb(255, 255, 255)
SAVE_NOTICE_COLOR = image.Color.from_rgb(0, 255, 0)
SAVE_BUTTON = (350, 270, 120, 45)
SAMPLE_ANIMATION_SECONDS = 0.6
SAMPLE_ANIMATION_START_RADIUS = 20
SAMPLE_ANIMATION_END_RADIUS = 2
SAVE_NOTICE_SECONDS = 1.5
TOUCH_POLL_INTERVAL_MS = 20
TOUCH_STARTUP_GUARD_MS = 2000
TASK_START_BUTTON = (130, 112, 220, 64)
TRACK_CAL_ENTRY = (130, 210, 220, 52)
RETRY_START_BUTTON = (350, 32, 120, 42)
COUNTDOWN_PANEL = (145, 38, 190, 148)
COUNTDOWN_SEGMENT_SCALE = 8
TRACK_CAL_BUTTONS = (
    ((5, 216, 150, 42), COMMAND_CAL_WHITE, "采集白色"),
    ((165, 216, 150, 42), COMMAND_CAL_BLACK, "采集黑色"),
    ((325, 216, 150, 42), COMMAND_FLASH_WRITE, "写入闪存"),
    ((5, 268, 230, 42), COMMAND_FLASH_READ, "读取闪存"),
    ((245, 268, 230, 42), 0, "退出校准"),
)
TRACK_CAL_COLOR = image.Color.from_rgb(40, 80, 180)
TRACK_CAL_BUTTON_COLOR = image.Color.from_rgb(55, 55, 55)
TRACK_CAL_DISABLED_COLOR = image.Color.from_rgb(25, 25, 25)
TRACK_CAL_ACTIVE_COLOR = image.Color.from_rgb(220, 180, 0)
TRACK_CAL_BACKGROUND = image.Color.from_rgb(0, 0, 0)
TRACK_CAL_FEEDBACK_SECONDS = 0.25
PROFILE_DEBUG = False
PROFILE_INTERVAL_SECONDS = 1.0
_TOUCH_STATE = None
_DISPLAY = None
_SERIAL = None
_SERIAL_STATE = None


class TouchState:
    def __init__(self):
        self.lock = Lock()
        self.pending_point = None
        self.save_requested = False
        self.track_cal_requested = False
        self.track_command = None
        self.track_exit_requested = False
        self.task_start_requested = False
        self.status = "READY"
        self.enabled = True
        self.mode = "startup"

    def set_point(self, x, y):
        with self.lock:
            self.pending_point = (x, y)

    def request_save(self):
        with self.lock:
            self.save_requested = True

    def request_track_cal(self):
        with self.lock:
            self.track_cal_requested = True

    def request_track_command(self, command):
        with self.lock:
            self.track_command = command

    def request_track_exit(self):
        with self.lock:
            self.track_exit_requested = True

    def request_task_start(self):
        with self.lock:
            self.task_start_requested = True

    def set_mode(self, mode):
        with self.lock:
            self.mode = mode

    def get_mode(self):
        with self.lock:
            return self.mode

    def take_events(self):
        with self.lock:
            point = self.pending_point
            save_requested = self.save_requested
            track_cal_requested = self.track_cal_requested
            track_command = self.track_command
            track_exit_requested = self.track_exit_requested
            task_start_requested = self.task_start_requested
            self.pending_point = None
            self.save_requested = False
            self.track_cal_requested = False
            self.track_command = None
            self.track_exit_requested = False
            self.task_start_requested = False
            return (
                point,
                save_requested,
                track_cal_requested,
                track_command,
                track_exit_requested,
                task_start_requested,
            )

    def set_status(self, status):
        with self.lock:
            self.status = status

    def disable(self):
        with self.lock:
            self.enabled = False

    def enable(self):
        with self.lock:
            was_disabled = not self.enabled
            self.enabled = True
            return was_disabled

    def is_enabled(self):
        with self.lock:
            return self.enabled

    def get_status(self):
        with self.lock:
            return self.status


class SerialState:
    def __init__(self):
        self.lock = Lock()
        self.pending_packet = None
        self.control_packets = []
        self.rx_buffer = bytearray()
        self.current_white = None
        self.current_black = None
        self.sampled_white = None
        self.sampled_black = None
        self.calibration_status = "正在读取当前参数"
        self.publish_count = 0
        self.send_count = 0
        self.replace_count = 0
        self.error_count = 0
        self.task_ready = False
        self.task_start_result = None

    def publish(self, packet):
        with self.lock:
            self.publish_count += 1
            if self.pending_packet is not None:
                self.replace_count += 1
            self.pending_packet = packet

    def request(self, command):
        with self.lock:
            if command == COMMAND_FLASH_WRITE and (
                self.sampled_white is None or self.sampled_black is None
            ):
                self.calibration_status = "请先完成白色和黑色采集"
                return False
            self.control_packets.append(make_request(command))
            self.calibration_status = "命令发送中"
            return True

    def take(self):
        with self.lock:
            if self.control_packets:
                return self.control_packets.pop(0)
            packet = self.pending_packet
            self.pending_packet = None
            return packet

    def mark_sent(self):
        with self.lock:
            self.send_count += 1

    def mark_error(self):
        with self.lock:
            self.error_count += 1

    def append_rx(self, data):
        with self.lock:
            self.rx_buffer.extend(data)
            while True:
                start = self.rx_buffer.find(b"\x5A\x5B")
                if start < 0:
                    self.rx_buffer.clear()
                    return
                if start > 0:
                    del self.rx_buffer[:start]
                if len(self.rx_buffer) < 3:
                    return
                length = self.rx_buffer[2]
                if length < 6 or length > 38:
                    del self.rx_buffer[0]
                    continue
                if len(self.rx_buffer) < length:
                    return
                packet = bytes(self.rx_buffer[:length])
                del self.rx_buffer[:length]
                if packet[-1] == 0xA5:
                    self._handle_response(packet)

    def _handle_response(self, packet):
        command = packet[3]
        status = packet[4]
        payload = packet[5:-1]
        if command == COMMAND_TASK_READY and status != STATUS_ERROR:
            self.task_ready = True
            return
        if command == COMMAND_START_TASK:
            self.task_start_result = status
            if status != STATUS_ERROR:
                self.task_ready = False
            return
        if COMMAND_CAL_WHITE <= command <= COMMAND_FLASH_READ:
            if status == STATUS_ERROR:
                self.calibration_status = "STM32执行失败"
            elif status == STATUS_DEFAULTS:
                self.calibration_status = "使用默认参数"
            else:
                self.calibration_status = "执行成功"
        if command == COMMAND_CAL_WHITE and len(payload) == 16 and status != STATUS_ERROR:
            self.sampled_white = list(unpack("<8H", payload))
        elif command == COMMAND_CAL_BLACK and len(payload) == 16 and status != STATUS_ERROR:
            self.sampled_black = list(unpack("<8H", payload))
        elif command == COMMAND_FLASH_READ and len(payload) == 32:
            values = unpack("<16H", payload)
            self.current_white = list(values[:8])
            self.current_black = list(values[8:])
        elif command == COMMAND_FLASH_WRITE and status != STATUS_ERROR:
            self.current_white = list(self.sampled_white)
            self.current_black = list(self.sampled_black)

    def is_task_ready(self):
        with self.lock:
            return self.task_ready

    def take_task_start_result(self):
        with self.lock:
            result = self.task_start_result
            self.task_start_result = None
            return result

    def get_calibration(self):
        with self.lock:
            return (
                None if self.current_white is None else list(self.current_white),
                None if self.current_black is None else list(self.current_black),
                None if self.sampled_white is None else list(self.sampled_white),
                None if self.sampled_black is None else list(self.sampled_black),
                self.calibration_status,
            )

    def get_stats(self):
        with self.lock:
            return (
                self.publish_count,
                self.send_count,
                self.replace_count,
                self.error_count,
            )


def point_in_rect(x, y, rect):
    return rect[0] <= x < rect[0] + rect[2] and rect[1] <= y < rect[1] + rect[3]


def touch_to_frame_point(x, y):
    disp = _DISPLAY
    if disp is None:
        return None
    point = image.resize_map_pos_reverse(
        FRAME_WIDTH,
        FRAME_HEIGHT,
        disp.width(),
        disp.height(),
        image.Fit.FIT_CONTAIN,
        x,
        y,
    )
    if len(point) < 2:
        return None
    return int(point[0]), int(point[1])


def touch_worker(_args):
    state = _TOUCH_STATE
    if state is None:
        return
    time.sleep_ms(TOUCH_STARTUP_GUARD_MS)
    if app.need_exit() or not state.is_enabled():
        return
    touch = touchscreen.TouchScreen()
    pressed_already = False
    while not app.need_exit() and state.is_enabled():
        if not touch.available(0):
            time.sleep_ms(TOUCH_POLL_INTERVAL_MS)
            continue
        x, y, pressed = touch.read0()
        if pressed:
            pressed_already = True
        elif pressed_already:
            pressed_already = False
            frame_point = touch_to_frame_point(x, y)
            if frame_point is None:
                time.sleep_ms(1)
                continue
            frame_x, frame_y = frame_point
            mode = state.get_mode()
            if mode == "startup":
                if point_in_rect(frame_x, frame_y, TRACK_CAL_ENTRY):
                    state.request_track_cal()
            elif mode == "startup_ready":
                if point_in_rect(frame_x, frame_y, TASK_START_BUTTON):
                    state.request_task_start()
                elif point_in_rect(frame_x, frame_y, TRACK_CAL_ENTRY):
                    state.request_track_cal()
            elif mode == "track_cal":
                for rect, command, _label in TRACK_CAL_BUTTONS:
                    if point_in_rect(frame_x, frame_y, rect):
                        if command == 0:
                            state.request_track_exit()
                        else:
                            state.request_track_command(command)
                        break
            elif mode == "vision_retry" and point_in_rect(
                frame_x, frame_y, RETRY_START_BUTTON
            ):
                state.request_task_start()
            elif point_in_rect(frame_x, frame_y, SAVE_BUTTON):
                state.request_save()
            elif 0 <= frame_x < FRAME_WIDTH and 0 <= frame_y < FRAME_HEIGHT:
                state.set_point(frame_x, frame_y)
        time.sleep_ms(TOUCH_POLL_INTERVAL_MS)


def serial_worker(_args):
    state = _SERIAL_STATE
    serial = _SERIAL
    if state is None or serial is None:
        return
    while not app.need_exit():
        packet = state.take()
        if packet is not None:
            try:
                serial.write(packet)
                state.mark_sent()
            except Exception:
                state.mark_error()
        try:
            if hasattr(serial, "any") and callable(serial.any):
                available = serial.any()
                data = serial.read(available) if available and available > 0 else None
            else:
                data = serial.read(38)
            if data:
                state.append_rx(bytes(data))
        except Exception:
            state.mark_error()
        time.sleep_ms(2)


def clamp(value, lower, upper):
    return max(lower, min(upper, value))


def rgb_to_lab(red, green, blue):
    def linearize(value):
        value /= 255.0
        return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4

    red = linearize(red)
    green = linearize(green)
    blue = linearize(blue)
    x = (red * 0.4124 + green * 0.3576 + blue * 0.1805) / 0.95047
    y = (red * 0.2126 + green * 0.7152 + blue * 0.0722) / 1.00000
    z = (red * 0.0193 + green * 0.1192 + blue * 0.9505) / 1.08883

    def pivot(value):
        return value ** (1.0 / 3.0) if value > 0.008856 else (7.787 * value) + (16.0 / 116.0)

    x, y, z = pivot(x), pivot(y), pivot(z)
    return round(116.0 * y - 16.0), round(500.0 * (x - y)), round(200.0 * (y - z))


def threshold_from_rgb(rgb):
    lab = rgb_to_lab(rgb[0], rgb[1], rgb[2])
    return [[
        clamp(lab[0] - THRESHOLD_DELTA, 0, 100),
        clamp(lab[0] + THRESHOLD_DELTA, 0, 100),
        clamp(lab[1] - THRESHOLD_DELTA, -128, 127),
        clamp(lab[1] + THRESHOLD_DELTA, -128, 127),
        clamp(lab[2] - THRESHOLD_DELTA, -128, 127),
        clamp(lab[2] + THRESHOLD_DELTA, -128, 127),
    ]]


def valid_thresholds(value):
    if not isinstance(value, list) or len(value) != 1 or len(value[0]) != 6:
        return False
    limits = ((0, 100), (0, 100), (-128, 127), (-128, 127), (-128, 127), (-128, 127))
    return all(isinstance(item, int) and limits[index][0] <= item <= limits[index][1]
               for index, item in enumerate(value[0]))


def load_thresholds():
    try:
        with open(THRESHOLD_PATH, "r") as threshold_file:
            saved = json.load(threshold_file).get("thresholds")
        return saved if valid_thresholds(saved) else DEFAULT_THRESHOLDS
    except (OSError, ValueError, TypeError, AttributeError):
        return DEFAULT_THRESHOLDS


def save_thresholds(thresholds):
    with open(THRESHOLD_PATH, "w") as threshold_file:
        json.dump({"thresholds": thresholds}, threshold_file)


def make_packet(detected, center_x=0, center_y=0, width=0, height=0):
    flags = 0x01 if detected else 0x00
    return pack(
        "<BBBBBHHHHB",
        0x5A,
        0x5B,
        PACKET_SIZE,
        COMMAND_FLOWER,
        flags,
        center_x,
        center_y,
        width,
        height,
        0xA5,
    )


def make_request(command):
    return pack("<BBBBB", 0x5A, 0x5B, 5, command, 0xA5)


def load_chinese_font():
    try:
        image.load_font("zh", "/usr/share/fonts/wqy-zenhei/wqy-zenhei.ttc", size=24)
        return True
    except Exception as error:
        print("MAIX_CAL font load failed:", error)
        return False


def draw_text(frame, x, y, chinese, fallback, color, has_chinese_font):
    if has_chinese_font:
        frame.draw_string(x, y, chinese, color=color, font="zh")
    else:
        frame.draw_string(x, y, fallback, color=color)


def draw_button(frame, rect, chinese, fallback, color, has_chinese_font):
    frame.draw_rect(rect[0], rect[1], rect[2], rect[3], color=color, thickness=-1)
    text_width = len(chinese) * 24 if has_chinese_font else len(fallback) * 8
    draw_text(
        frame,
        rect[0] + max(6, (rect[2] - text_width) // 2),
        rect[1] + 9,
        chinese,
        fallback,
        UI_COLOR,
        has_chinese_font,
    )


def draw_countdown_digit(frame, x, y, digit, scale, color):
    segments = (
        (1, 0, 4, 1),
        (5, 1, 1, 3),
        (5, 5, 1, 3),
        (1, 8, 4, 1),
        (0, 5, 1, 3),
        (0, 1, 1, 3),
        (1, 4, 4, 1),
    )
    digit_segments = (
        (0, 1, 2, 3, 4, 5),
        (1, 2),
        (0, 1, 3, 4, 6),
        (0, 1, 2, 3, 6),
        (1, 2, 5, 6),
        (0, 2, 3, 5, 6),
        (0, 2, 3, 4, 5, 6),
        (0, 1, 2),
        (0, 1, 2, 3, 4, 5, 6),
        (0, 1, 2, 3, 5, 6),
    )
    for segment_index in digit_segments[digit]:
        segment = segments[segment_index]
        frame.draw_rect(
            x + segment[0] * scale,
            y + segment[1] * scale,
            segment[2] * scale,
            segment[3] * scale,
            color=color,
            thickness=-1,
        )


def draw_countdown(frame, seconds, has_chinese_font):
    panel_x, panel_y, panel_width, panel_height = COUNTDOWN_PANEL
    value = max(0, min(99, int(seconds + 0.99)))
    digits = str(value)
    scale = COUNTDOWN_SEGMENT_SCALE
    digit_width = 6 * scale
    gap = scale
    total_width = len(digits) * digit_width + (len(digits) - 1) * gap
    digit_x = panel_x + (panel_width - total_width) // 2

    frame.draw_rect(
        panel_x,
        panel_y,
        panel_width,
        panel_height,
        color=TRACK_CAL_BACKGROUND,
        thickness=-1,
    )
    frame.draw_rect(
        panel_x,
        panel_y,
        panel_width,
        panel_height,
        color=FPS_COLOR,
        thickness=3,
    )
    draw_text(
        frame,
        panel_x + 22,
        panel_y + 10,
        "等待相机稳定",
        "CAMERA STABLE",
        FPS_COLOR,
        has_chinese_font,
    )
    for digit in digits:
        draw_countdown_digit(
            frame,
            digit_x,
            panel_y + 52,
            int(digit),
            scale,
            FPS_COLOR,
        )
        digit_x += digit_width + gap


def draw_startup(
    frame,
    remaining_seconds,
    has_chinese_font,
    task_start_active,
):
    ready = remaining_seconds <= 0.0
    if ready:
        draw_text(
            frame, 145, 42, "相机已稳定", "CAMERA READY",
            SAVE_NOTICE_COLOR, has_chinese_font
        )
        draw_text(
            frame, 125, 76, "请按下按键启动任务", "PRESS START",
            FPS_COLOR, has_chinese_font
        )
        draw_button(
            frame,
            TASK_START_BUTTON,
            "启动任务",
            "START TASK",
            TRACK_CAL_ACTIVE_COLOR if task_start_active else TRACK_CAL_COLOR,
            has_chinese_font,
        )
    else:
        draw_countdown(frame, remaining_seconds, has_chinese_font)
    draw_button(
        frame, TRACK_CAL_ENTRY, "进入循迹校准", "TRACK CAL",
        TRACK_CAL_COLOR, has_chinese_font
    )


def draw_calibration_values(frame, y, label, fallback, values, has_chinese_font):
    draw_text(frame, 8, y, label, fallback, FPS_COLOR, has_chinese_font)
    if values is not None:
        frame.draw_string(66, y + 5, " ".join(str(value) for value in values), color=UI_COLOR)


def draw_tracking_calibration(
    frame,
    serial_state,
    has_chinese_font,
    active_command,
    now,
    feedback_until,
):
    current_white, current_black, sampled_white, sampled_black, status = \
        serial_state.get_calibration()
    draw_text(frame, 145, 2, "循迹传感器校准", "TRACK CAL", SAVE_NOTICE_COLOR, has_chinese_font)
    draw_text(frame, 8, 34, "当前参数", "CURRENT", SAVE_NOTICE_COLOR, has_chinese_font)
    draw_calibration_values(frame, 59, "白", "W", current_white, has_chinese_font)
    draw_calibration_values(frame, 85, "黑", "B", current_black, has_chinese_font)
    draw_text(frame, 8, 116, "本轮采集", "SAMPLED", SAVE_NOTICE_COLOR, has_chinese_font)
    draw_calibration_values(frame, 141, "白", "W", sampled_white, has_chinese_font)
    draw_calibration_values(frame, 167, "黑", "B", sampled_black, has_chinese_font)
    draw_text(frame, 8, 193, status, status, UI_COLOR, has_chinese_font)
    write_ready = sampled_white is not None and sampled_black is not None
    for rect, command, label in TRACK_CAL_BUTTONS:
        if command == active_command and now < feedback_until:
            color = TRACK_CAL_ACTIVE_COLOR
        elif command == COMMAND_FLASH_WRITE and not write_ready:
            color = TRACK_CAL_DISABLED_COLOR
        else:
            color = TRACK_CAL_BUTTON_COLOR
        draw_button(frame, rect, label, label, color, has_chinese_font)


def draw_sample_animation(frame, animation, now):
    if animation is None:
        return None
    x, y, start_time = animation
    elapsed = now - start_time
    if elapsed >= SAMPLE_ANIMATION_SECONDS:
        return None
    progress = max(0.0, elapsed / SAMPLE_ANIMATION_SECONDS)
    radius = int(
        SAMPLE_ANIMATION_START_RADIUS
        + (SAMPLE_ANIMATION_END_RADIUS - SAMPLE_ANIMATION_START_RADIUS) * progress
    )
    frame.draw_circle(x, y, max(1, radius), SAMPLE_COLOR, thickness=2)
    return animation


def main():
    global _TOUCH_STATE, _DISPLAY, _SERIAL, _SERIAL_STATE

    has_chinese_font = load_chinese_font()
    serial = UART(UART_DEVICE, UART_BAUD_RATE)
    serial_state = SerialState()
    _SERIAL = serial
    _SERIAL_STATE = serial_state
    cam = camera.Camera(
        width=FRAME_WIDTH,
        height=FRAME_HEIGHT,
        buff_num=CAMERA_BUFFER_COUNT,
    )
    disp = display.Display()
    track_cal_frame = image.Image(
        width=FRAME_WIDTH,
        height=FRAME_HEIGHT,
        format=image.Format.FMT_RGB888,
    )
    _DISPLAY = disp
    try:
        sensor_size = cam.get_sensor_size()
    except Exception:
        sensor_size = (-1, -1)
    print(
        "MAIX_VIDEO capture={}x{} sensor={}x{} display={}x{} fit=contain".format(
            cam.width(),
            cam.height(),
            sensor_size[0],
            sensor_size[1],
            disp.width(),
            disp.height(),
        )
    )
    thresholds = load_thresholds()
    touch_state = TouchState()
    _TOUCH_STATE = touch_state
    thread.Thread(touch_worker).detach()
    thread.Thread(serial_worker).detach()

    sample_animation = None
    save_notice_until = 0.0
    calibration_enabled = True
    fps = 0
    display_frame_counter = 0
    time.fps_start()
    profile_start = time.time()
    profile_frames = 0
    profile_display_frames = 0
    profile_blobs = 0
    profile_read_seconds = 0.0
    profile_events_seconds = 0.0
    profile_detect_seconds = 0.0
    profile_draw_seconds = 0.0
    profile_show_seconds = 0.0
    profile_total_seconds = 0.0
    run_mode = "startup"
    startup_started = time.time()
    active_track_button = None
    track_button_feedback_until = 0.0
    track_exit_at = 0.0
    task_start_feedback_until = 0.0
    task_start_at = 0.0
    retry_button_visible = False
    retry_button_hide_at = 0.0

    while not app.need_exit():
        frame_start = time.time()
        frame = cam.read()
        read_end = time.time()
        now = read_end
        fps = int(time.fps() + 0.5)

        events_start = time.time()
        sample_point = None
        save_requested = False
        track_cal_requested = False
        track_command = None
        track_exit_requested = False
        task_start_requested = False
        task_ready = serial_state.is_task_ready()
        task_start_result = serial_state.take_task_start_result()
        if run_mode == "vision" and task_ready:
            retry_button_visible = True
            retry_button_hide_at = 0.0
            if touch_state.enable():
                thread.Thread(touch_worker).detach()
            touch_state.set_mode("vision_retry")
        if run_mode == "vision" and retry_button_visible and (
            task_start_result is not None
        ):
            if task_start_result == STATUS_ERROR:
                retry_button_hide_at = 0.0
                touch_state.set_mode("vision_retry")
            else:
                retry_button_hide_at = max(now, task_start_feedback_until)
        if touch_state.is_enabled():
            sample_point, save_requested, track_cal_requested, track_command, \
                track_exit_requested, task_start_requested = \
                touch_state.take_events()
        if run_mode == "startup":
            if track_cal_requested:
                run_mode = "track_cal"
                touch_state.set_mode("track_cal")
                serial_state.request(COMMAND_FLASH_READ)
            elif task_start_requested and (
                (now - startup_started) >= STARTUP_STABLE_SECONDS
            ):
                serial_state.request(COMMAND_START_TASK)
                task_start_feedback_until = now + TRACK_CAL_FEEDBACK_SECONDS
                task_start_at = task_start_feedback_until
                touch_state.set_mode("startup_starting")
            elif task_start_at > 0.0 and now >= task_start_at:
                run_mode = "vision"
                touch_state.set_mode("vision")
                task_start_at = 0.0
            elif (now - startup_started) >= STARTUP_STABLE_SECONDS:
                touch_state.set_mode("startup_ready")
        elif run_mode == "track_cal":
            if track_command is not None:
                active_track_button = track_command
                track_button_feedback_until = now + TRACK_CAL_FEEDBACK_SECONDS
                serial_state.request(track_command)
            if track_exit_requested:
                active_track_button = 0
                track_button_feedback_until = now + TRACK_CAL_FEEDBACK_SECONDS
                track_exit_at = track_button_feedback_until
            if track_exit_at > 0.0 and now >= track_exit_at:
                run_mode = "startup"
                if (now - startup_started) >= STARTUP_STABLE_SECONDS:
                    touch_state.set_mode("startup_ready")
                else:
                    touch_state.set_mode("startup")
                track_exit_at = 0.0
        elif run_mode == "vision":
            if task_start_requested and retry_button_visible:
                sample_point = None
                save_requested = False
                serial_state.request(COMMAND_START_TASK)
                task_start_feedback_until = now + TRACK_CAL_FEEDBACK_SECONDS
            if retry_button_hide_at > 0.0 and now >= retry_button_hide_at:
                retry_button_visible = False
                retry_button_hide_at = 0.0
                touch_state.set_mode("vision")
            if calibration_enabled and save_requested:
                sample_point = None
                try:
                    save_thresholds(thresholds)
                    touch_state.set_status("SAVED")
                    touch_state.disable()
                    calibration_enabled = False
                    sample_animation = None
                    save_notice_until = now + SAVE_NOTICE_SECONDS
                except OSError:
                    touch_state.set_status("SAVE ERR")
            elif calibration_enabled and sample_point is not None:
                rgb = frame.get_pixel(sample_point[0], sample_point[1], rgbtuple=True)
                if len(rgb) >= 3:
                    thresholds = threshold_from_rgb(rgb)
                    touch_state.set_status("SAMPLED")
                    sample_animation = (sample_point[0], sample_point[1], now)
        events_end = time.time()

        detect_start = time.time()
        blobs = []
        if run_mode == "vision":
            blobs = frame.find_blobs(
                thresholds,
                x_stride=BLOB_X_STRIDE,
                y_stride=BLOB_Y_STRIDE,
                area_threshold=AREA_THRESHOLD,
                pixels_threshold=PIXELS_THRESHOLD,
                merge=True,
                margin=MERGE_MARGIN,
            )
        detect_end = time.time()
        render_start = detect_end

        target_geometry = None
        if run_mode == "vision" and blobs:
            target = max(blobs, key=lambda blob: blob[2] * blob[3])
            x, y, width, height = target[0], target[1], target[2], target[3]
            center_x = x + width // 2
            center_y = y + height // 2
            target_geometry = (x, y, width, height, center_x, center_y)
            serial_state.publish(make_packet(True, center_x, center_y, width, height))
        elif run_mode == "vision":
            serial_state.publish(make_packet(False))

        display_frame_counter += 1
        should_display = display_frame_counter >= DISPLAY_FRAME_DIVIDER
        if should_display:
            display_frame_counter = 0
            display_frame = frame
            if run_mode == "startup":
                draw_startup(
                    frame,
                    STARTUP_STABLE_SECONDS - (now - startup_started),
                    has_chinese_font,
                    now < task_start_feedback_until,
                )
            elif run_mode == "track_cal":
                track_cal_frame.draw_rect(
                    0,
                    0,
                    FRAME_WIDTH,
                    FRAME_HEIGHT,
                    color=TRACK_CAL_BACKGROUND,
                    thickness=-1,
                )
                draw_tracking_calibration(
                    track_cal_frame,
                    serial_state,
                    has_chinese_font,
                    active_track_button,
                    now,
                    track_button_feedback_until,
                )
                display_frame = track_cal_frame
            elif target_geometry is not None:
                x, y, width, height, center_x, center_y = target_geometry
                frame.draw_rect(x, y, width, height, MARK_COLOR, thickness=2)
                frame.draw_cross(center_x, center_y, MARK_COLOR, size=10, thickness=2)
                frame.draw_string(
                    x,
                    max(0, y - 20),
                    "C:{},{}".format(center_x, center_y),
                    color=MARK_COLOR,
                )

            if run_mode == "vision":
                sample_animation = draw_sample_animation(frame, sample_animation, now)
                frame.draw_string(
                    FRAME_WIDTH - 65,
                    5,
                    "FPS:{}".format(fps),
                    color=FPS_COLOR,
                )
                if retry_button_visible:
                    draw_button(
                        frame,
                        RETRY_START_BUTTON,
                        "启动任务",
                        "START TASK",
                        TRACK_CAL_ACTIVE_COLOR
                        if now < task_start_feedback_until
                        else TRACK_CAL_COLOR,
                        has_chinese_font,
                    )
            if run_mode == "vision" and calibration_enabled:
                frame.draw_rect(
                    SAVE_BUTTON[0],
                    SAVE_BUTTON[1],
                    SAVE_BUTTON[2],
                    SAVE_BUTTON[3],
                    UI_COLOR,
                    2,
                )
                frame.draw_string(
                    SAVE_BUTTON[0] + 25,
                    SAVE_BUTTON[1] + 12,
                    "SAVE",
                    color=UI_COLOR,
                )
            if run_mode == "vision" and now < save_notice_until:
                frame.draw_string(340, 247, "THRESHOLD SAVED", color=SAVE_NOTICE_COLOR)
        render_end = time.time()
        show_start = render_end
        if should_display:
            disp.show(display_frame, fit=image.Fit.FIT_CONTAIN)
        frame_end = time.time()

        if PROFILE_DEBUG:
            profile_frames += 1
            if should_display:
                profile_display_frames += 1
            profile_blobs += len(blobs)
            profile_read_seconds += read_end - frame_start
            profile_events_seconds += events_end - events_start
            profile_detect_seconds += detect_end - detect_start
            profile_draw_seconds += render_end - render_start
            profile_show_seconds += frame_end - show_start
            profile_total_seconds += frame_end - frame_start
            profile_elapsed = frame_end - profile_start
            if profile_elapsed >= PROFILE_INTERVAL_SECONDS:
                serial_stats = serial_state.get_stats()
                try:
                    camera_fps = cam.fps()
                except Exception:
                    camera_fps = -1.0
                frame_count = max(1, profile_frames)
                display_count = max(1, profile_display_frames)
                print(
                    "MAIX_PROFILE profile=on frames={} display_calls={} loop_fps={:.1f} "
                    "display_rate={:.1f} display_fps={} "
                    "camera_fps={:.1f} read_ms={:.2f} events_ms={:.2f} "
                    "find_ms={:.2f} draw_ms={:.2f} show_ms={:.2f} "
                    "draw_call_ms={:.2f} show_call_ms={:.2f} total_ms={:.2f} "
                    "blobs={} uart_pub={} uart_sent={} uart_replace={} uart_err={}".format(
                        profile_frames,
                        profile_display_frames,
                        profile_frames / profile_elapsed,
                        profile_display_frames / profile_elapsed,
                        fps,
                        camera_fps,
                        profile_read_seconds * 1000 / frame_count,
                        profile_events_seconds * 1000 / frame_count,
                        profile_detect_seconds * 1000 / frame_count,
                        profile_draw_seconds * 1000 / frame_count,
                        profile_show_seconds * 1000 / frame_count,
                        profile_draw_seconds * 1000 / display_count,
                        profile_show_seconds * 1000 / display_count,
                        profile_total_seconds * 1000 / frame_count,
                        profile_blobs,
                        serial_stats[0],
                        serial_stats[1],
                        serial_stats[2],
                        serial_stats[3],
                    )
                )
                profile_start = frame_end
                profile_frames = 0
                profile_display_frames = 0
                profile_blobs = 0
                profile_read_seconds = 0.0
                profile_events_seconds = 0.0
                profile_detect_seconds = 0.0
                profile_draw_seconds = 0.0
                profile_show_seconds = 0.0
                profile_total_seconds = 0.0


if __name__ == "__main__":
    main()
