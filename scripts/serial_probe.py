#!/usr/bin/env python3
"""
serial_probe.py - probe serial port with several baudrates and flow-control options
Usage:
  python scripts/serial_probe.py COM4 --send "help\n"

This will attempt a small set of configurations and print the first bytes received.
"""
import serial
import time
import argparse

BAUDS = [15000000, 921600, 460800, 230400, 115200]
FLOW_OPTIONS = [
    {'rtscts': False, 'dsrdtr': False, 'xonxoff': False},
    {'rtscts': True, 'dsrdtr': False, 'xonxoff': False},
    {'rtscts': False, 'dsrdtr': True, 'xonxoff': False},
    {'rtscts': False, 'dsrdtr': False, 'xonxoff': True},
]


def try_open(port, baud, opts, send_cmd=None, read_time=2.0):
    try:
        # default framing 8N1
        s = serial.Serial(port, baudrate=baud, timeout=0.5,
                          rtscts=opts['rtscts'], dsrdtr=opts['dsrdtr'], xonxoff=opts['xonxoff'],
                          bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE)
    except Exception as e:
        return (False, f"open_error: {e}")
    try:
        s.reset_input_buffer()
        s.reset_output_buffer()
        if send_cmd:
            try:
                s.write(send_cmd.encode())
                s.flush()
            except Exception as e:
                pass
        deadline = time.time() + read_time
        data = b''
        while time.time() < deadline:
            chunk = s.read(256)
            if chunk:
                data += chunk
            else:
                time.sleep(0.05)
        s.close()
        if data:
            return (True, data)
        else:
            return (True, b'')
    except Exception as e:
        try:
            s.close()
        except:
            pass
        return (False, f"runtime_error: {e}")


def main():
    p = argparse.ArgumentParser()
    p.add_argument('port')
    p.add_argument('--send', help='Send this command (e.g. "help\\n")')
    p.add_argument('--read-time', type=float, default=2.0, help='Seconds to wait for response per try')
    args = p.parse_args()

    any_seen = False
    for baud in BAUDS:
        for opts in FLOW_OPTIONS:
            print(f"Trying baud={baud} rtscts={opts['rtscts']} dsrdtr={opts['dsrdtr']} xonxoff={opts['xonxoff']}")
            ok, res = try_open(args.port, baud, opts, send_cmd=args.send, read_time=args.read_time)
            if not ok:
                print("  Failed to open or runtime error:", res)
                continue
            if isinstance(res, bytes) and len(res) > 0:
                any_seen = True
                try:
                    text = res.decode(errors='replace')
                except Exception:
                    text = None
                print("  -> Received bytes (len=%d):" % len(res))
                if text is not None:
                    print(text)
                print('  hex:', ' '.join(f"{b:02X}" for b in res[:200]))
                print()
            else:
                print("  -> No data")
    if not any_seen:
        print("No data seen for any configuration. If VS Code serial monitor works, ensure it is closed before running this probe.")


if __name__ == '__main__':
    main()
