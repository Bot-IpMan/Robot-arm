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

