#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
improved_arduino_monitor.py
===========================

Мета:
• Надійно читати рядкові дані з Arduino через /dev/arduino (udev-лінк).
• Автоматично відновлювати з’єднання після виривання/вставляння USB.
• Передавати прочитані рядки далі:
    – друк у stdout (може забирати Node-RED Exec node або systemd-журнал);
    – *опційно* публікація у MQTT-брокер (вмикається прапорцем).

Формат очікуваного рядка (приклад):
    "AHT20 T: 26.45 C, H: 49.80 % | BMP280 T: 27.42 C, P: 985.48 hPa | MQ-9: 127"

Автор: Дмитро <you@example.com>
"""

import os
import sys
import time
import logging
import argparse
from pathlib import Path

try:
    import serial
    from serial.serialutil import SerialException
except ImportError:
    sys.exit("❌ Встановіть pyserial:  sudo pip3 install pyserial")

# ────────────────────────────────────────────────────────────────────
# Налаштування за замовчуванням (можна перевизначити аргументами CLI)
# ────────────────────────────────────────────────────────────────────
DEFAULT_PORT  = "/dev/arduino"
DEFAULT_BAUD  = 9600
DEFAULT_MQTT  = False
DEFAULT_TOPIC = "sensors/raw"

# Якщо плануєте MQTT – розкоментуйте й встановіть paho-mqtt
try:
    import paho.mqtt.client as mqtt  # noqa: E402
except ModuleNotFoundError:
    mqtt = None  # Можна працювати без MQTT


# ────────────────────────────────
# Функції допоміжні
# ────────────────────────────────
def now() -> str:
    """Повертає час у форматі HH:MM:SS для друку."""
    return time.strftime("%H:%M:%S")


def setup_logger(verbosity: int) -> logging.Logger:
    """Конфігурує логер із кольоровим виводом у systemd-журнал чи консоль."""
    lvl = logging.DEBUG if verbosity > 1 else logging.INFO if verbosity == 1 else logging.WARNING
    logging.basicConfig(
        level=lvl,
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%H:%M:%S",
        handlers=[logging.StreamHandler(sys.stdout)],
    )
    return logging.getLogger("arduino-monitor")


def open_serial(port: str, baud: int, log: logging.Logger) -> serial.Serial:
    """Відкриває серійний порт із нескінченними ретраями."""
    backoff = 2  # cекунди між спробами
    while True:
        try:
            ser = serial.Serial(port, baud, timeout=2)
            log.info(f"🔌 Підключено до {port} @ {baud} бод")
            # «чистимо» буфер, щоб не зловити обрізаний рядок
            ser.reset_input_buffer()
            return ser
        except SerialException as e:
            log.warning(f"⚠️  {e}. Повтор через {backoff}s…")
            time.sleep(backoff)
            backoff = min(backoff * 2, 30)  # експон. backoff до 30 с


def mqtt_connect(broker: str, port: int, log: logging.Logger):
    """Повертає з’єднаний MQTT-клієнт або None."""
    if mqtt is None:
        log.error("paho-mqtt не встановлено, MQTT вимкнено")
        return None
    client = mqtt.Client()
    try:
        client.connect(broker, port, keepalive=60)
        client.loop_start()
        log.info(f"📡 Підключено до MQTT {broker}:{port}")
        return client
    except Exception as e:
        log.error(f"🚫 MQTT підключення провалене: {e}")
        return None


# ────────────────────────────────
# Головний цикл
# ────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="Надійний читач Serial → stdout (+MQTT)",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--port", default=os.getenv("ARDUINO_PORT", DEFAULT_PORT), help="серійний порт або udev-лінк")
    parser.add_argument("--baud", default=int(os.getenv("ARDUINO_BAUD", DEFAULT_BAUD)), type=int, help="швидкість бод")
    parser.add_argument("--mqtt", action="store_true", default=DEFAULT_MQTT, help="публікувати у MQTT")
    parser.add_argument("--mqtt-host", default=os.getenv("MQTT_HOST", "localhost"), help="MQTT broker host")
    parser.add_argument("--mqtt-port", default=int(os.getenv("MQTT_PORT", 1883)), type=int, help="MQTT port")
    parser.add_argument("--topic", default=os.getenv("MQTT_TOPIC", DEFAULT_TOPIC), help="MQTT topic для raw-даних")
    parser.add_argument("-v", "--verbose", action="count", default=0, help="рівень подробиць (-v, -vv)")
    args = parser.parse_args()

    log = setup_logger(args.verbose)

    # Підключення MQTT (якщо увімкнено)
    mqtt_client = mqtt_connect(args.mqtt_host, args.mqtt_port, log) if args.mqtt else None

    # Нескінченний цикл «відкрий порт → читай рядки → при втраті — відкрити знову»
    while True:
        ser = open_serial(args.port, args.baud, log)
        while True:
            try:
                raw = ser.readline()              # bytes
                if not raw:
                    continue                      # timeout без даних
                line = raw.decode(errors="ignore").strip()
                if not line:
                    continue
                # 1) Друк у stdout (бачить systemd-journal, Node-RED Exec інпут)
                print(line, flush=True)

                # 2) MQTT (якщо увімкнено)
                if mqtt_client:
                    mqtt_client.publish(args.topic, payload=line, qos=0, retain=False)

            except SerialException as e:
                log.warning(f"🔌 З’єднання втрачено: {e}")
                try:
                    ser.close()
                except Exception:
                    pass
                break  # виходимо у зовнішній while True → reopen

            except KeyboardInterrupt:
                log.info("⏹️  Зупинено користувачем (Ctrl-C)")
                try:
                    ser.close()
                except Exception:
                    pass
                if mqtt_client:
                    mqtt_client.loop_stop()
                    mqtt_client.disconnect()
                sys.exit(0)

            except Exception as e:
                log.error(f"Неочікувана помилка: {e}")
                # продовжуємо цикл — не валимо сервіс


# ────────────────────────────────
if __name__ == "__main__":
    main()
