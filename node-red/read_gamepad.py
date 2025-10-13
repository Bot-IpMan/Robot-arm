#!/usr/bin/env python3
"""Read TX12 joystick from /dev/input/js0 and output axes/buttons as JSON.

The script reads all available events from the joystick device within a short
interval and prints a JSON object of the form:
    {"axes": [...], "buttons": [...]}. 

It is designed to be called from Node-RED using a python node. When the device
cannot be accessed, an error is printed to stderr and the script exits with
status 1 so that Node-RED can report the failure (rc:1).
To avoid hammering the joystick device when Node-RED triggers the helper very
frequently, a small refresh interval is enforced; calls within that window are
served from the cached state written to ``TX12_STATE_PATH``.
"""
import json
import os
import select
import struct
import sys
import time
from typing import Dict, List, Tuple

import fcntl

DEVICE = os.environ.get("TX12_DEVICE", "/dev/input/js0")
STATE_PATH = os.environ.get("TX12_STATE_PATH", "/tmp/read_gamepad_state.json")
LOCK_PATH = os.environ.get("TX12_LOCK_PATH", "/tmp/read_gamepad.lock")

# Timing parameters (seconds).  They can be overridden from the environment to
# better align the helper with the Node-RED inject interval.
READ_WINDOW = max(0.0, float(os.environ.get("TX12_READ_WINDOW", "0.75")))
EVENT_EXTENSION = max(0.0, float(os.environ.get("TX12_EVENT_EXTENSION", "0.15")))
SELECT_TIMEOUT = max(0.0, float(os.environ.get("TX12_SELECT_TIMEOUT", "0.05")))
# Throttle full reads when Node-RED invokes the helper too frequently.
MIN_REFRESH_INTERVAL = max(
    0.0, float(os.environ.get("TX12_MIN_REFRESH_INTERVAL", "0.2"))
)

DEBUG = os.environ.get("TX12_DEBUG", "0").lower() in {"1", "true", "yes", "on"}

JS_EVENT_FORMAT = "IhBB"  # time, value, type, number
JS_EVENT_SIZE = struct.calcsize(JS_EVENT_FORMAT)
JS_EVENT_BUTTON = 0x01
JS_EVENT_AXIS = 0x02

# We expect at least four axes and four buttons for the project
AXES_COUNT = 4
BUTTON_COUNT = 4


def _normalise(values: List[int], count: int) -> List[int]:
    cleaned: List[int] = []
    for value in values[:count]:
        try:
            cleaned.append(int(value))
        except (TypeError, ValueError):
            cleaned.append(0)
    if len(cleaned) < count:
        cleaned.extend([0] * (count - len(cleaned)))
    return cleaned


def _load_previous_state() -> Tuple[List[int], List[int], float]:
    try:
        with open(STATE_PATH, "r", encoding="utf-8") as state_file:
            state = json.load(state_file)
    except FileNotFoundError:
        return [0] * AXES_COUNT, [0] * BUTTON_COUNT, 0.0
    except (json.JSONDecodeError, TypeError, ValueError):
        return [0] * AXES_COUNT, [0] * BUTTON_COUNT, 0.0

    axes = state.get("axes", []) if isinstance(state, dict) else []
    buttons = state.get("buttons", []) if isinstance(state, dict) else []
    timestamp = state.get("timestamp", 0.0) if isinstance(state, dict) else 0.0
    try:
        last_read = float(timestamp)
    except (TypeError, ValueError):
        last_read = 0.0

    return _normalise(axes, AXES_COUNT), _normalise(buttons, BUTTON_COUNT), last_read


def _save_state(axes: List[int], buttons: List[int], timestamp: float) -> None:
    tmp_path = f"{STATE_PATH}.tmp"
    with open(tmp_path, "w", encoding="utf-8") as state_file:
        json.dump({"axes": axes, "buttons": buttons, "timestamp": timestamp}, state_file)
    os.replace(tmp_path, STATE_PATH)


def _debug(msg: str) -> None:
    if DEBUG:
        print(msg, file=sys.stderr, flush=True)


def _ensure_parent_dir(path: str) -> None:
    parent = os.path.dirname(path)
    if parent and not os.path.isdir(parent):
        os.makedirs(parent, exist_ok=True)


def read_gamepad() -> Dict[str, List[int]]:
    _ensure_parent_dir(STATE_PATH)
    _ensure_parent_dir(LOCK_PATH)

    with open(LOCK_PATH, "w", encoding="utf-8") as lock_file:
        try:
            fcntl.flock(lock_file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            _debug("read_gamepad: another instance is active, reusing cached state")
            axes, buttons = _load_previous_state()
            return {"axes": axes, "buttons": buttons}

        axes, buttons, last_read_at = _load_previous_state()
        now = time.time()

        if MIN_REFRESH_INTERVAL > 0 and now - last_read_at < MIN_REFRESH_INTERVAL:
            _debug(
                "read_gamepad: skip refresh (%.3fs < %.3fs)"
                % (now - last_read_at, MIN_REFRESH_INTERVAL)
            )
            return {"axes": axes, "buttons": buttons}

        with open(DEVICE, "rb") as jsdev:
            start_time = time.monotonic()
            end_time = start_time + READ_WINDOW
            got_event = False
            event_count = 0

            _debug(
                f"read_gamepad: start window={READ_WINDOW}s select={SELECT_TIMEOUT}s "
                f"extend={EVENT_EXTENSION}s"
            )

            while True:
                now = time.monotonic()
                if now >= end_time:
                    break

                timeout = min(SELECT_TIMEOUT, max(0.0, end_time - now))
                r, _, _ = select.select([jsdev], [], [], timeout)
                if not r:
                    continue

                while True:
                    data = jsdev.read(JS_EVENT_SIZE)
                    if not data:
                        break

                    _time, value, ev_type, number = struct.unpack(JS_EVENT_FORMAT, data)
                    if ev_type & JS_EVENT_AXIS and number < AXES_COUNT:
                        axes[number] = value
                    elif ev_type & JS_EVENT_BUTTON and number < BUTTON_COUNT:
                        buttons[number] = value

                    # Drain any immediately available events before returning
                    if select.select([jsdev], [], [], 0)[0]:
                        continue
                    break

                got_event = True
                event_count += 1
                if got_event:
                    end_time = time.monotonic() + EVENT_EXTENSION

            duration = time.monotonic() - start_time
            _debug(
                "read_gamepad: events=%d duration=%.3fs axes=%s buttons=%s"
                % (event_count, duration, axes, buttons)
            )

        _save_state(axes, buttons, time.time())

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
