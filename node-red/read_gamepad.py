#!/usr/bin/env python3
"""Read TX12 joystick from /dev/input/js0 and output axes/buttons as JSON.

The script reads all available events from the joystick device within a short
interval and prints a JSON object of the form:
    {"axes": [...], "buttons": [...]}.

It is designed to be called from Node-RED using a python node. When the device
cannot be accessed, an error is printed to stderr and the script exits with
status 1 so that Node-RED can report the failure (rc:1).
"""
import json
import os
import select
import struct
import sys
from typing import Dict, List

DEVICE = os.environ.get("TX12_DEVICE", "/dev/input/js0")

JS_EVENT_FORMAT = "IhBB"  # time, value, type, number
JS_EVENT_SIZE = struct.calcsize(JS_EVENT_FORMAT)
JS_EVENT_BUTTON = 0x01
JS_EVENT_AXIS = 0x02

# We expect at least four axes and four buttons for the project
AXES_COUNT = 4
BUTTON_COUNT = 4


def read_gamepad() -> Dict[str, List[int]]:
    axes = [0] * AXES_COUNT
    buttons = [0] * BUTTON_COUNT

    with open(DEVICE, "rb") as jsdev:
        # collect events for a short period (~1 s total)
        for _ in range(20):
            r, _, _ = select.select([jsdev], [], [], 0.05)
            if not r:
                continue
            data = jsdev.read(JS_EVENT_SIZE)
            if not data:
                break
            _time, value, ev_type, number = struct.unpack(JS_EVENT_FORMAT, data)
            if ev_type & JS_EVENT_AXIS and number < AXES_COUNT:
                axes[number] = value
            elif ev_type & JS_EVENT_BUTTON and number < BUTTON_COUNT:
                buttons[number] = value
    return {"axes": axes, "buttons": buttons}


def main() -> int:
    try:
        state = read_gamepad()
    except FileNotFoundError:
        print(json.dumps({"error": f"device {DEVICE} not found"}), file=sys.stderr)
        return 1
    except Exception as exc:  # pragma: no cover - unexpected errors
        print(json.dumps({"error": str(exc)}), file=sys.stderr)
        return 1
    print(json.dumps(state), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
