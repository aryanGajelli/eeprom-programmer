#!/usr/bin/env python3
"""
Simple serial monitor for debugging device responses.
Usage:
  python scripts/monitor_serial.py COM4 --baud 15000000
  python scripts/monitor_serial.py /dev/ttyUSB0 --baud 115200 --send "help\n"

This prints timestamps, printable text, and hex for non-printable bytes.
"""
import time
import serial

ser = serial.Serial(port="COM4", baudrate=15000000)

ser.write(b"help\n")
time.sleep(0.01)  # wait for response to arrive
res = ser.read_all()  # clear buffer
print(res.decode())
ser.close()