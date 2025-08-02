#!/bin/bash

echo "=== USB Devices Check ==="
lsusb

echo -e "\n=== Serial Devices Check ==="
ls -la /dev/tty*

echo -e "\n=== USB Serial Devices ==="
ls -la /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || echo "No USB serial devices found"

echo -e "\n=== Kernel Messages (last 20) ==="
dmesg | tail -20

echo -e "\n=== USB Device Tree ==="
find /sys/bus/usb/devices -name "tty*" -exec ls -la {} \; 2>/dev/null