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
from struct import pack
from threading import Lock

import cv2

from maix import app, camera, display, image, thread, time
from maix import touchscreen
from maix.v1.machine import UART

FRAME_WIDTH = 480
FRAME_HEIGHT = 320
UART_DEVICE = "/dev/ttyS0"
UART_BAUD_RATE = 115200
PACKET_SIZE = 13
THRESHOLD_PATH = "/root/maixcam_threshold.json"
THRESHOLD_DELTA = 20
DEFAULT_THRESHOLDS = [[45, 100, -80, -5, -10, 60]]
PIXELS_THRESHOLD = 500
AREA_THRESHOLD = 500
CAMERA_BUFFER_COUNT = 2
DETECT_DOWNSCALE = 2
DISPLAY_ENABLED = False
DISPLAY_FRAME_DIVIDER = 3
MORPH_KERNEL_SIZE = 5
OPENCV_THREAD_COUNT = 1
PROFILE_DEBUG = True
PROFILE_INTERVAL_SECONDS = 1.0
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
_TOUCH_STATE = None
_DISPLAY = None
_SERIAL = None
_SERIAL_STATE = None


class TouchState:
    def __init__(self):
        self.lock = Lock()
        self.pending_point = None
        self.save_requested = False
        self.status = "READY"

    def set_point(self, x, y):
        with self.lock:
            self.pending_point = (x, y)

    def request_save(self):
        with self.lock:
            self.save_requested = True

    def take_events(self):
        with self.lock:
            point = self.pending_point
            save_requested = self.save_requested
            self.pending_point = None
            self.save_requested = False
            return point, save_requested

    def set_status(self, status):
        with self.lock:
            self.status = status


class SerialState:
    def __init__(self):
        self.lock = Lock()
        self.pending_packet = None
        self.publish_count = 0
        self.send_count = 0
        self.replace_count = 0
        self.error_count = 0

    def publish(self, packet):
        with self.lock:
            self.publish_count += 1
            if self.pending_packet is not None:
                self.replace_count += 1
            self.pending_packet = packet

    def take(self):
        with self.lock:
            packet = self.pending_packet
            self.pending_packet = None
            return packet

    def mark_sent(self):
        with self.lock:
            self.send_count += 1

    def mark_error(self):
        with self.lock:
            self.error_count += 1

    def get_stats(self):
        with self.lock:
            return self.publish_count, self.send_count, self.replace_count, self.error_count


def point_in_rect(x, y, rect):
    return rect[0] <= x < rect[0] + rect[2] and rect[1] <= y < rect[1] + rect[3]


def touch_to_frame_point(x, y):
    if _DISPLAY is None:
        return None
    point = image.resize_map_pos_reverse(
        FRAME_WIDTH,
        FRAME_HEIGHT,
        _DISPLAY.width(),
        _DISPLAY.height(),
        image.Fit.FIT_CONTAIN,
        x,
        y,
    )
    if len(point) < 2:
        return None
    return int(point[0]), int(point[1])


def touch_worker(_args):
    if _TOUCH_STATE is None:
        return
    touch = touchscreen.TouchScreen()
    pressed_already = False
    while not app.need_exit():
        if not touch.available(5):
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
            if point_in_rect(frame_x, frame_y, SAVE_BUTTON):
                _TOUCH_STATE.request_save()
            elif 0 <= frame_x < FRAME_WIDTH and 0 <= frame_y < FRAME_HEIGHT:
                _TOUCH_STATE.set_point(frame_x, frame_y)


def serial_worker(_args):
    if _SERIAL_STATE is None or _SERIAL is None:
        return
    while not app.need_exit():
        packet = _SERIAL_STATE.take()
        if packet is None:
            time.sleep_ms(2)
            continue
        try:
            _SERIAL.write(packet)
            _SERIAL_STATE.mark_sent()
        except Exception:
            _SERIAL_STATE.mark_error()
            time.sleep_ms(1)


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
    y = red * 0.2126 + green * 0.7152 + blue * 0.0722
    z = (red * 0.0193 + green * 0.1192 + blue * 0.9505) / 1.08883

    def pivot(value):
        return value ** (1.0 / 3.0) if value > 0.008856 else 7.787 * value + 16.0 / 116.0

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
    return all(
        isinstance(item, int) and limits[index][0] <= item <= limits[index][1]
        for index, item in enumerate(value[0])
    )


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
    return pack(
        "<BBBBHHHHB",
        0x5A,
        0x5B,
        PACKET_SIZE,
        0x01 if detected else 0x00,
        center_x,
        center_y,
        width,
        height,
        0xA5,
    )


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


def lab_threshold_to_opencv(threshold):
    return (
        int(round(threshold[0] * 255.0 / 100.0)),
        threshold[2] + 128,
        threshold[4] + 128,
    ), (
        int(round(threshold[1] * 255.0 / 100.0)),
        threshold[3] + 128,
        threshold[5] + 128,
    )


def find_largest_blob(frame_cv, lower_lab, upper_lab, morphology_kernel):
    resize_end = time.time()
    lab_frame = cv2.cvtColor(frame_cv, cv2.COLOR_BGR2LAB)
    lab_end = time.time()
    mask = cv2.inRange(lab_frame, lower_lab, upper_lab)
    mask_end = time.time()
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, morphology_kernel)
    morphology_end = time.time()
    component_count, _, stats, _ = cv2.connectedComponentsWithStats(mask, connectivity=8)
    components_end = time.time()

    scale_area = DETECT_DOWNSCALE * DETECT_DOWNSCALE
    minimum_pixels = max(1, (PIXELS_THRESHOLD + scale_area - 1) // scale_area)
    best_geometry = None
    best_area = -1
    for label in range(1, component_count):
        x = int(stats[label, cv2.CC_STAT_LEFT])
        y = int(stats[label, cv2.CC_STAT_TOP])
        width = int(stats[label, cv2.CC_STAT_WIDTH])
        height = int(stats[label, cv2.CC_STAT_HEIGHT])
        pixels = int(stats[label, cv2.CC_STAT_AREA])
        if pixels < minimum_pixels or width * height * scale_area < AREA_THRESHOLD:
            continue
        area = width * height
        if area > best_area:
            best_area = area
            best_geometry = (
                x * DETECT_DOWNSCALE,
                y * DETECT_DOWNSCALE,
                min(FRAME_WIDTH - x * DETECT_DOWNSCALE, width * DETECT_DOWNSCALE),
                min(FRAME_HEIGHT - y * DETECT_DOWNSCALE, height * DETECT_DOWNSCALE),
            )

    return (
        best_geometry,
        resize_end,
        lab_end,
        mask_end,
        morphology_end,
        components_end,
        component_count - 1,
    )


def main():
    global _DISPLAY, _SERIAL, _SERIAL_STATE, _TOUCH_STATE

    cv2.setNumThreads(OPENCV_THREAD_COUNT)
    cv2.setUseOptimized(True)
    print(
        "MAIX_CV_CONFIG detect={}x{} downscale={} cv_threads={} optimized={} display={}".format(
            FRAME_WIDTH // DETECT_DOWNSCALE,
            FRAME_HEIGHT // DETECT_DOWNSCALE,
            DETECT_DOWNSCALE,
            cv2.getNumThreads(),
            int(cv2.useOptimized()),
            int(DISPLAY_ENABLED),
        )
    )

    serial = UART(UART_DEVICE, UART_BAUD_RATE)
    serial_state = SerialState()
    _SERIAL = serial
    _SERIAL_STATE = serial_state
    detect_cam = camera.Camera(
        width=FRAME_WIDTH // DETECT_DOWNSCALE,
        height=FRAME_HEIGHT // DETECT_DOWNSCALE,
        format=image.Format.FMT_BGR888,
        buff_num=CAMERA_BUFFER_COUNT,
    )
    display_cam = None
    if DISPLAY_ENABLED:
        display_cam = detect_cam.add_channel(
            width=FRAME_WIDTH,
            height=FRAME_HEIGHT,
            format=image.Format.FMT_BGR888,
            buff_num=CAMERA_BUFFER_COUNT,
        )
    disp = display.Display()
    _DISPLAY = disp
    thresholds = load_thresholds()
    lower_lab, upper_lab = lab_threshold_to_opencv(thresholds[0])
    morphology_kernel = cv2.getStructuringElement(
        cv2.MORPH_RECT,
        (MORPH_KERNEL_SIZE, MORPH_KERNEL_SIZE),
    )
    touch_state = TouchState()
    _TOUCH_STATE = touch_state
    thread.Thread(touch_worker).detach()
    thread.Thread(serial_worker).detach()

    sample_animation = None
    save_notice_until = 0.0
    display_frame_counter = 0
    time.fps_start()
    profile_start = time.time()
    profile_frames = 0
    profile_display_frames = 0
    profile_components = 0
    profile_detected_frames = 0
    profile_latest_target = (0, 0, 0, 0)
    profile_read_seconds = 0.0
    profile_events_seconds = 0.0
    profile_convert_seconds = 0.0
    profile_resize_seconds = 0.0
    profile_lab_seconds = 0.0
    profile_mask_seconds = 0.0
    profile_morphology_seconds = 0.0
    profile_components_seconds = 0.0
    profile_draw_seconds = 0.0
    profile_show_seconds = 0.0
    profile_total_seconds = 0.0

    while not app.need_exit():
        frame_start = time.time()
        frame = detect_cam.read()
        read_end = time.time()
        now = read_end
        fps = int(time.fps() + 0.5)

        events_start = time.time()
        sample_point, save_requested = touch_state.take_events()
        if save_requested:
            sample_point = None
            try:
                save_thresholds(thresholds)
                touch_state.set_status("SAVED")
                save_notice_until = now + SAVE_NOTICE_SECONDS
            except OSError:
                touch_state.set_status("SAVE ERR")
        elif sample_point is not None:
            sample_x = sample_point[0] // DETECT_DOWNSCALE
            sample_y = sample_point[1] // DETECT_DOWNSCALE
            rgb = frame.get_pixel(sample_x, sample_y, rgbtuple=True)
            if len(rgb) >= 3:
                thresholds = threshold_from_rgb(rgb)
                lower_lab, upper_lab = lab_threshold_to_opencv(thresholds[0])
                touch_state.set_status("SAMPLED")
                sample_animation = (sample_point[0], sample_point[1], now)
        events_end = time.time()

        convert_start = time.time()
        frame_cv = image.image2cv(frame, ensure_bgr=False, copy=False)
        convert_end = time.time()
        (
            target_geometry,
            resize_end,
            lab_end,
            mask_end,
            morphology_end,
            components_end,
            component_count,
        ) = find_largest_blob(frame_cv, lower_lab, upper_lab, morphology_kernel)

        if target_geometry is not None:
            x, y, width, height = target_geometry
            center_x = x + width // 2
            center_y = y + height // 2
            target_geometry = (x, y, width, height, center_x, center_y)
            profile_detected_frames += 1
            profile_latest_target = (center_x, center_y, width, height)
            serial_state.publish(make_packet(True, center_x, center_y, width, height))
        else:
            serial_state.publish(make_packet(False))

        render_start = time.time()
        display_frame_counter += 1
        should_display = DISPLAY_ENABLED and display_frame_counter >= DISPLAY_FRAME_DIVIDER
        if should_display:
            display_frame_counter = 0
            display_frame = display_cam.read()
            if target_geometry is not None:
                x, y, width, height, center_x, center_y = target_geometry
                display_frame.draw_rect(x, y, width, height, MARK_COLOR, thickness=2)
                display_frame.draw_cross(center_x, center_y, MARK_COLOR, size=10, thickness=2)
                display_frame.draw_string(
                    x,
                    max(0, y - 20),
                    "C:{},{}".format(center_x, center_y),
                    color=MARK_COLOR,
                )
            sample_animation = draw_sample_animation(display_frame, sample_animation, now)
            display_frame.draw_string(
                FRAME_WIDTH - 65,
                5,
                "FPS:{}".format(fps),
                color=FPS_COLOR,
            )
            display_frame.draw_rect(
                SAVE_BUTTON[0],
                SAVE_BUTTON[1],
                SAVE_BUTTON[2],
                SAVE_BUTTON[3],
                UI_COLOR,
                2,
            )
            display_frame.draw_string(
                SAVE_BUTTON[0] + 25,
                SAVE_BUTTON[1] + 12,
                "SAVE",
                color=UI_COLOR,
            )
            if now < save_notice_until:
                display_frame.draw_string(340, 247, "THRESHOLD SAVED", color=SAVE_NOTICE_COLOR)
        render_end = time.time()

        show_start = render_end
        if should_display:
            disp.show(display_frame, fit=image.Fit.FIT_CONTAIN)
        frame_end = time.time()

        if PROFILE_DEBUG:
            profile_frames += 1
            profile_display_frames += int(should_display)
            profile_components += component_count
            profile_read_seconds += read_end - frame_start
            profile_events_seconds += events_end - events_start
            profile_convert_seconds += convert_end - convert_start
            profile_resize_seconds += resize_end - convert_end
            profile_lab_seconds += lab_end - resize_end
            profile_mask_seconds += mask_end - lab_end
            profile_morphology_seconds += morphology_end - mask_end
            profile_components_seconds += components_end - morphology_end
            profile_draw_seconds += render_end - render_start
            profile_show_seconds += frame_end - show_start
            profile_total_seconds += frame_end - frame_start
            profile_elapsed = frame_end - profile_start
            if profile_elapsed >= PROFILE_INTERVAL_SECONDS:
                frame_count = max(1, profile_frames)
                display_count = max(1, profile_display_frames)
                serial_stats = serial_state.get_stats()
                print(
                    "MAIX_CV_PROFILE frames={} display_calls={} loop_fps={:.1f} "
                    "read_ms={:.2f} events_ms={:.2f} cv_view_ms={:.2f} "
                    "resize_ms={:.2f} lab_ms={:.2f} "
                    "mask_ms={:.2f} morph_ms={:.2f} components_ms={:.2f} "
                    "draw_ms={:.2f} show_call_ms={:.2f} total_ms={:.2f} "
                    "components={} detected_frames={} target={},{},{},{} "
                    "uart_pub={} uart_sent={} uart_replace={} uart_err={}".format(
                        profile_frames,
                        profile_display_frames,
                        profile_frames / profile_elapsed,
                        profile_read_seconds * 1000 / frame_count,
                        profile_events_seconds * 1000 / frame_count,
                        profile_convert_seconds * 1000 / frame_count,
                        profile_resize_seconds * 1000 / frame_count,
                        profile_lab_seconds * 1000 / frame_count,
                        profile_mask_seconds * 1000 / frame_count,
                        profile_morphology_seconds * 1000 / frame_count,
                        profile_components_seconds * 1000 / frame_count,
                        profile_draw_seconds * 1000 / display_count,
                        profile_show_seconds * 1000 / display_count,
                        profile_total_seconds * 1000 / frame_count,
                        profile_components,
                        profile_detected_frames,
                        profile_latest_target[0],
                        profile_latest_target[1],
                        profile_latest_target[2],
                        profile_latest_target[3],
                        serial_stats[0],
                        serial_stats[1],
                        serial_stats[2],
                        serial_stats[3],
                    )
                )
                profile_start = frame_end
                profile_frames = 0
                profile_display_frames = 0
                profile_components = 0
                profile_detected_frames = 0
                profile_latest_target = (0, 0, 0, 0)
                profile_read_seconds = 0.0
                profile_events_seconds = 0.0
                profile_convert_seconds = 0.0
                profile_resize_seconds = 0.0
                profile_lab_seconds = 0.0
                profile_mask_seconds = 0.0
                profile_morphology_seconds = 0.0
                profile_components_seconds = 0.0
                profile_draw_seconds = 0.0
                profile_show_seconds = 0.0
                profile_total_seconds = 0.0


if __name__ == "__main__":
    main()
