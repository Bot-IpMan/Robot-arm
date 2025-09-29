# Robot Arm Control via Node-RED and TX12 Joystick

This project demonstrates how to control a 5-axis robotic arm based on an Arduino Mega 2560 and a PCA9685 PWM driver using a Radiomaster TX12 transmitter. Node-RED acts as the control hub: it reads joystick events from Linux (`/dev/input/js0`), maps them to servo angles, and sends commands to the Arduino over a serial connection.

## Repository layout

| Path | Description |
| ---- | ----------- |
| `node-red/read_gamepad.py` | Python helper that polls `/dev/input/js0` and prints joystick state as JSON |
| `node-red/flows_tx12.json` | Node-RED flow wiring joystick data through processing nodes to a serial output |
| `BMP280/BMP280-v2.ino` | Arduino sketch that accepts commands like `S1:90;GRIP:OPEN` and drives servos via PCA9685 |
| `USB-Devices-Check.sh`, `arduino_handshake.sh`, `install_arduino_monitor.sh`, `improved_arduino_monitor.py` | Utility scripts for serial communication |

## Hardware requirements

- **Arduino Mega 2560** connected to a PCA9685 16-channel PWM driver
- **Five MG90S servo motors** (base, shoulder, elbow, wrist, gripper)
- **Radiomaster TX12** (appears as `/dev/input/js0` when plugged in via USB)
- **Host PC** running Linux with available serial port `/dev/ttyACM0`

## Software prerequisites

1. **Node.js** and **Node-RED**
2. Node-RED nodes:
   - [`node-red-node-python`](https://flows.nodered.org/node/node-red-node-python) or `node-red-contrib-python3-function`
   - Built-in `serial` nodes
3. **Python 3** (for `read_gamepad.py`)
4. **Arduino IDE** or `arduino-cli` to upload the sketch

## Upload the Arduino sketch

1. Open `BMP280/BMP280-v2.ino` in the Arduino IDE
2. Select **Arduino Mega 2560** and the correct serial port (e.g. `/dev/ttyACM0`)
3. Upload the sketch

The sketch listens for newline‑terminated commands such as:

- `S1:45` – move base servo to 45°
- `GRIP:OPEN` / `GRIP:CLOSE`
- `HOME`, `READY`, `STOP`, `STATUS`

## Import the Node-RED flow

1. Copy `node-red/read_gamepad.py` to your Node-RED user directory and mark it executable (`chmod +x`)
2. Start Node-RED and open the editor
3. From the menu select **Import** → **Clipboard** and paste the contents of `node-red/flows_tx12.json`
4. Deploy the flow

### Flow overview

1. **inject** – triggers the Python script on start
2. **python3** (`read_gamepad.py`) – emits joystick JSON
3. **function 4** – parses the JSON payload into `{axes, buttons}`
4. **function 3** – converts axes/buttons to servo commands with dead‑zone filtering
5. **serial out** – sends commands to `/dev/ttyACM0`
6. **debug** – shows parsed values and generated commands

## Running

1. Connect the TX12 controller (should appear as `/dev/input/js0`)
2. Connect the Arduino to `/dev/ttyACM0`
3. Run Node-RED (`node-red` command or system service)
4. Move sticks or press buttons – the robotic arm should respond

### Verify joystick detection

If the joystick is not reacting inside Node-RED, first confirm that Linux
recognises it as an input device. The TX12 should expose a joystick at
`/dev/input/js0` with the friendly name “OpenTX Radiomaster TX12
Joystick”. The following commands help to confirm that state:

```bash
grep -i "tx12" /proc/bus/input/devices
ls -l /dev/input/js0
```

The first command should list a block similar to:

```
N: Name="OpenTX Radiomaster TX12 Joystick"
H: Handlers=js0 eventX
```

The second command should show the character device with read/write
permissions (e.g. `crw-rw-rw-`), confirming that the `joydev` kernel
module has created `/dev/input/js0` and that it is readable by the
Node-RED service. If the joystick appears under a different device file
you can override the default used by `read_gamepad.py` via the
`TX12_DEVICE` environment variable.

### Isolate joystick and helper script issues

If Node-RED still receives only zero values, test the joystick and the
Python helper independently to narrow down the fault:

1. **Exercise the kernel driver.** Run `jstest /dev/input/js0` (from the
   `joystick` package) or `evtest /dev/input/js0` and move both sticks.
   Axis and button values should change in the terminal. If they remain
   at zero, the problem lies with the joystick hardware, USB cable, or
   kernel drivers rather than with Node-RED.
2. **Run the helper directly.** Execute `python3 node-red/read_gamepad.py`
   in a terminal and move the sticks while it is running. The script
   prints JSON snapshots such as `{"axes": {...}, "buttons": {...}}`.
   If everything is wired correctly the axis values should briefly
   deviate from zero. When testing, begin with a short pause and then
   move the sticks so that at least one input event is captured before
   the script exits.
3. **Mind the dead zone.** The `function 3` node in `flows_tx12.json`
   filters out joystick movements whose absolute axis value is below
   3000. Make sure that any test movement exceeds this threshold;
   otherwise, the flow intentionally ignores the change.

### Common causes for empty joystick readings

If `read_gamepad.py` works only sporadically or reports only zeros
inside Node-RED, double-check the following points:

- **Adjust the sampling window.** The helper now waits up to 0.75 s for
  input events and extends the window by 0.15 s after each event. These
  values can be tuned via the `TX12_READ_WINDOW`, `TX12_EVENT_EXTENSION`
  and `TX12_SELECT_TIMEOUT` environment variables, e.g. set
  `TX12_READ_WINDOW=1.2` to give the controller more time to send its
  first event or lower the values to tighten the response time once the
  system is stable.
- **Customise helper paths.** If you change `TX12_DEVICE`,
  `TX12_STATE_PATH` or `TX12_LOCK_PATH` to store data outside `/tmp`, the
  script now creates any missing parent directories automatically before
  opening the files, which avoids crashes due to missing folders.
- **Enable debug logging.** Set `TX12_DEBUG=1` on the Python node to log
  how many events were captured during each poll along with the final
  axis/button state. The messages appear in the Node-RED log and make it
  easy to see whether the script captured only zeros or whether non-zero
  data is lost in later flow nodes.
- **Inject node runs too frequently.** When the inject node fires every
  0.2 s but the Python script needs close to a second to finish, multiple
  instances overlap and compete for the joystick device. Use a longer
  inject interval or redesign the script as a long-running loop that
  keeps the device open while publishing results back to Node-RED.
- **Device permissions.** On most systems `/dev/input/js0` is
  world-readable (`0666`). If your Node-RED service runs under a
  restricted account, ensure it still has access either by adding the
  user to the `input` group or by adjusting udev rules.
- **Python environment.** Confirm that Node-RED calls the expected
  Python interpreter and that `read_gamepad.py` resides inside the
  Node-RED user directory. From a shell, run the script manually using
  the same path Node-RED uses to rule out path or virtual environment
  issues.

When in doubt, detach the script from Node-RED entirely and run it in a
loop from the terminal while moving the sticks. Once you see non-zero
values there, reconnect it to the flow and verify that the serial node
forwards the generated commands to the Arduino.

### Verify Node-RED environment

Ensure the Node-RED user directory contains `read_gamepad.py`, marked as
executable (`chmod +x`). The flow expects a Python integration node such
as [`node-red-contrib-python3-function`](https://flows.nodered.org/node/node-red-contrib-python3-function)
or [`node-red-node-python`](https://flows.nodered.org/node/node-red-node-python)
to spawn the helper script. When Node-RED starts, check the logs for a
message indicating that the Python node launched successfully and that it
is reading from the expected device path.

## Testing

After any changes run the following checks from the repository root:

```bash
python3 -m py_compile node-red/read_gamepad.py
python3 node-red/read_gamepad.py        # fails gracefully if js0 missing
python3 -m json.tool node-red/flows_tx12.json
```

## License

This project is provided as-is without any specific license. Use at your own risk.

