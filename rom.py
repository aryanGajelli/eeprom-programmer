with open("rom.bin", "wb") as f:
    f.write(b"\xea"*65536)