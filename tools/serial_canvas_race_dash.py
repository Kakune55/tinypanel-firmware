#!/usr/bin/env python3
"""TinyPanel Serial Canvas racing dashboard demo.

Enter Serial Canvas on the device first:
SYSTEM -> ACTION -> CANVAS, then long-press the key.

Examples:
    python3 tools/serial_canvas_race_dash.py --port /dev/ttyACM0
    python3 tools/serial_canvas_race_dash.py --port COM5
    python3 tools/serial_canvas_race_dash.py --dry-run --frames 1
"""

from __future__ import annotations

import argparse
import math
import sys
import time
from dataclasses import dataclass


RPM_MAX = 9000


@dataclass
class Telemetry:
    speed_kph: int
    rpm: int
    gear: str
    throttle: int
    brake: int
    fuel: int
    oil_c: int
    lap: str


class CanvasTransport:
    def __init__(self, port: str, baud: int, dry_run: bool, line_delay: float, dtr: bool) -> None:
        self.dry_run = dry_run
        self.line_delay = line_delay
        self.serial = None
        if dry_run:
            return

        try:
            import serial  # type: ignore
        except ImportError as exc:
            raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc

        self.serial = serial.Serial()
        self.serial.port = port
        self.serial.baudrate = baud
        self.serial.timeout = 0.2
        self.serial.write_timeout = 1.0
        self.serial.dtr = dtr
        self.serial.rts = False
        self.serial.open()
        self.serial.dtr = dtr
        self.serial.rts = False
        time.sleep(0.3)

    def batch(self, commands: list[str]) -> None:
        if self.dry_run:
            for command in commands:
                print(command)
            return

        assert self.serial is not None
        self.discard_available()
        for command in commands:
            self.serial.write(f"{command}\n".encode("ascii", errors="replace"))
            if self.line_delay > 0:
                time.sleep(self.line_delay)
        self.serial.flush()

    def read_available(self) -> str:
        if self.dry_run or self.serial is None:
            return ""
        time.sleep(0.05)
        waiting = self.serial.in_waiting
        if waiting <= 0:
            return ""
        return self.serial.read(waiting).decode("utf-8", errors="replace")

    def discard_available(self) -> None:
        if self.dry_run or self.serial is None:
            return
        waiting = self.serial.in_waiting
        if waiting > 0:
            self.serial.read(waiting)

    def close(self) -> None:
        if self.serial is not None:
            self.serial.close()


def clamp(value: int, low: int, high: int) -> int:
    return max(low, min(high, value))


def fill_bar(x: int, y: int, w: int, h: int, percent: int) -> list[str]:
    fill = clamp(round(w * percent / 100), 0, w)
    if fill <= 0:
        return []
    return [f"FILL {x} {y} {fill} {h} B"]


def synth_telemetry(frame: int, fps: float) -> Telemetry:
    t = frame / fps
    wave = (math.sin(t * 1.45) + 1.0) / 2.0
    brake_wave = max(0.0, math.sin(t * 0.85 + 2.3))
    speed = int(45 + wave * 235)
    rpm = int(2200 + wave * 6500)
    return Telemetry(
        speed_kph=speed,
        rpm=rpm,
        gear=str(clamp(int(speed / 48) + 1, 1, 6)),
        throttle=int(30 + wave * 70),
        brake=int(brake_wave * brake_wave * 85),
        fuel=clamp(86 - int(t / 2), 12, 100),
        oil_c=int(84 + math.sin(t * 0.31) * 8 + wave * 8),
        lap=f"1:{84.2 + math.sin(t * 0.18) * 1.7:04.1f}",
    )


def dashboard_shell() -> list[str]:
    return [
        "CLEAR W",
        "RECT 4 4 392 292 B",
        "LINE 4 52 396 52 B",
        "TEXT 14 14 2 B RACE MODE",
        "TEXT 306 16 1 B TPD1",
        "TEXT 18 60 1 B SPEED",
        "TEXT 166 60 1 B GEAR",
        "TEXT 266 60 1 B ENGINE",
        "RECT 14 76 126 96 B",
        "RECT 154 76 90 96 B",
        "RECT 258 76 126 96 B",
        "LINE 14 184 386 184 B",
        "TEXT 18 198 1 B THR",
        "TEXT 18 232 1 B BRK",
        "TEXT 212 198 1 B FUEL",
        "TEXT 212 232 1 B OIL",
        "TEXT 18 268 1 B LAP",
        "TEXT 212 268 1 B DELTA",
        "FLUSH",
    ]


def dashboard_frame(data: Telemetry) -> list[str]:
    commands = [
        "FILL 18 36 364 12 W",
        "FILL 24 94 104 48 W",
        "FILL 168 86 64 66 W",
        "FILL 270 94 100 52 W",
        "FILL 54 198 130 16 W",
        "FILL 54 232 130 16 W",
        "FILL 250 198 120 16 W",
        "FILL 250 232 120 16 W",
        "FILL 58 266 138 18 W",
        "FILL 258 266 90 18 W",
    ]

    segment_count = clamp(round(data.rpm / RPM_MAX * 14), 0, 14)
    for index in range(segment_count):
        x = 20 + index * 26
        h = 8 if index < 9 else 12
        y = 40 if index < 9 else 36
        commands.append(f"FILL {x} {y} 20 {h} B")

    rpm_percent = clamp(round(data.rpm / RPM_MAX * 100), 0, 100)
    commands.extend(fill_bar(270, 150, 100, 12, rpm_percent))
    commands.extend([
        f"TEXT 28 98 5 B {data.speed_kph:03d}",
        "TEXT 104 142 1 B KMH",
        f"TEXT 174 88 8 B {data.gear}",
        f"TEXT 276 98 3 B {data.rpm // 100:02d}",
        "TEXT 332 114 1 B x100",
        "RECT 270 150 100 12 B",
    ])

    commands.extend(fill_bar(56, 200, 126, 12, data.throttle))
    commands.extend(fill_bar(56, 234, 126, 12, data.brake))
    commands.extend(fill_bar(252, 200, 116, 12, data.fuel))
    commands.extend(fill_bar(252, 234, 116, 12, clamp((data.oil_c - 60) * 2, 0, 100)))
    commands.extend([
        "RECT 54 198 130 16 B",
        "RECT 54 232 130 16 B",
        "RECT 250 198 120 16 B",
        "RECT 250 232 120 16 B",
        f"TEXT 58 268 1 B {data.lap}",
        f"TEXT 258 268 1 B +0.{(data.speed_kph + data.rpm) % 10:01d}{(data.rpm // 100) % 10:01d}",
        "FLUSH",
    ])
    return commands


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="TinyPanel Serial Canvas racing dashboard demo")
    parser.add_argument("--port", default="/dev/ttyACM0", help="Serial port, for example /dev/ttyACM0 or COM5")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument("--fps", type=float, default=10.0, help="Dashboard update rate")
    parser.add_argument("--frames", type=int, default=0, help="Number of frames to send; 0 runs forever")
    parser.add_argument("--line-delay", type=float, default=0.001, help="Delay between command lines in seconds")
    parser.add_argument("--startup-delay", type=float, default=0.2, help="Delay after opening the port before drawing")
    parser.add_argument("--no-ping", action="store_true", help="Skip the startup PING probe")
    parser.add_argument("--no-dtr", action="store_true", help="Open the serial port with DTR disabled")
    parser.add_argument("--dry-run", action="store_true", help="Print commands instead of opening a serial port")
    parser.add_argument("--exit", action="store_true", help="Send EXIT when finished")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.fps <= 0:
        raise SystemExit("--fps must be greater than 0")

    transport = CanvasTransport(args.port, args.baud, args.dry_run, args.line_delay, not args.no_dtr)
    frame = 0
    frame_interval = 1.0 / args.fps
    next_frame = time.monotonic()

    try:
        if args.startup_delay > 0:
            time.sleep(args.startup_delay)
        if not args.no_ping:
            transport.batch(["PING"])
            response = transport.read_available()
            if response:
                print(response, end="")
        transport.batch(dashboard_shell())
        while args.frames == 0 or frame < args.frames:
            transport.batch(dashboard_frame(synth_telemetry(frame, args.fps)))
            frame += 1
            next_frame += frame_interval
            sleep_for = next_frame - time.monotonic()
            if sleep_for > 0:
                time.sleep(sleep_for)
            else:
                next_frame = time.monotonic()
    except KeyboardInterrupt:
        pass
    finally:
        if args.exit:
            transport.batch(["EXIT"])
        transport.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
