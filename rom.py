rom = bytearray([0xea]*32768)

# fmt: off
code = bytearray([
    0xa9, 0xff,        # LDA #$ff
    0x8d, 0x02, 0x60,  # STA $6002 ; DDRB = output

    0xa9, 0x55,        # LDA #$55
    0x8d, 0x00, 0x60,  # STA $6000 ; DDRB = 0x55 (01010101)

    0xa9, 0xaa,        # LDA #$aa
    0x8d, 0x00, 0x60,  # STA $6000 ; DDRB = 0xaa (10101010)
    
    0x4c, 0x05, 0x80   # JMP $8005
])
# fmt: on
rom[0:len(code)] = code

rom[0x7ffc] = 0x00
rom[0x7ffd] = 0x80


with open("rom.bin", "wb") as f:
    f.write(rom)
