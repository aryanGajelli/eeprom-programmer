import binascii
import time
import serial
import atexit
import colorama
from colorama import Fore
import argparse

colorama.init()

ser: serial.Serial = None


def compute_crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def recv(timeout=0.1):
    deadline = time.time() + timeout
    data = b''
    while ser.in_waiting > 0 or time.time() < deadline:
        chunk = ser.read_all()
        if chunk:
            data += chunk
        if chunk:
            print(f"{Fore.GREEN}{chunk.decode()}{Fore.RESET}", end="")

    return data.decode()


def send_and_recv(cmd, timeout=0.1):
    print(f"{Fore.BLUE}{cmd.strip()}{Fore.RESET}")
    ser.write(cmd.encode())
    return recv(timeout)


def parse_args():
    p = argparse.ArgumentParser(description="Send a binary file to the EEPROM programmer in bulk mode")
    p.add_argument('port', help='Serial port (e.g. COM4 or /dev/ttyUSB0)')
    p.add_argument('file', help='Binary file to send')
    p.add_argument("--verify-only", action="store_true", help="Only perform verification, do not write data")
    return p.parse_args()


if __name__ == "__main__":
    args = parse_args()
    ser = serial.Serial(port=args.port, baudrate=15000000)
    atexit.register(ser.close)

    res = send_and_recv("ping\n")  # clear buffer
    if "pong" not in res.strip():
        raise ConnectionError("Did not receive expected pong response; got: " + res)

    with open(args.file, "rb") as f:
        data = f.read()

    assert len(data) % 4096 == 0, f"Expected file size to be a multiple of 4096 bytes got {len(data)}"

    EEPROM_SIZE = 65536
    START_ADDRESS = 0x0000

    if START_ADDRESS + len(data) > EEPROM_SIZE:
        raise ValueError(f"Start address 0x{START_ADDRESS:04x} + {len(data):04x} is out of bounds for EEPROM size {EEPROM_SIZE} bytes")

    start = time.time()
    res = send_and_recv(f"bulkLoad 0x{START_ADDRESS:04x} {len(data)} {compute_crc32(data)}\n")
    if "READY" not in res.strip():
        raise RuntimeError("Did not receive expected READY response; got: " + res)

    CHUNK_SIZE = 256
    for chunk in range(0, len(data), CHUNK_SIZE):
        ser.write(data[chunk:chunk+CHUNK_SIZE])

    res = recv()
    if f"Bulk RX complete and verified (CRC ok), stored RAM bytes: {len(data)}" not in res:
        raise RuntimeError("Did not receive expected completion message; got: " + res)

    if not args.verify_only:
        res = send_and_recv("bulkCommit\n", timeout=1.0)
        while "Programming ... Done" not in res:
            res += recv()

    res = send_and_recv("bulkVerify\n")

    end = time.time()
    print(f"\nTotal time: {end - start:.3f} seconds")
