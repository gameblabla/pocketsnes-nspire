#!/usr/bin/env python3
"""Writes a tiny LoROM test program that stores 42 99 at the start of its
2 KB battery save and then loops forever. Used to check that in-game saves
are written to the calculator by themselves (without quitting)."""
import sys

rom = bytearray(0x8000)
code = bytes([
    0x78, 0x18, 0xFB, 0xE2, 0x30,        # sei; clc; xce; sep #$30
    0xA9, 0x42, 0x8F, 0x00, 0x00, 0x70,  # lda #$42; sta $700000
    0xA9, 0x99, 0x8F, 0x01, 0x00, 0x70,  # lda #$99; sta $700001
    0x80, 0xFE,                          # bra *
    0x40,                                # rti (NMI/IRQ handler)
])
rom[0:len(code)] = code
rti = 0x8000 + len(code) - 1


def put16(address, value):
    rom[address] = value & 0xFF
    rom[address + 1] = value >> 8


header = 0x7FC0
rom[header:header + 21] = b"SRAM WRITE TEST".ljust(21)
rom[header + 0x15] = 0x20   # LoROM
rom[header + 0x16] = 0x02   # ROM + RAM + battery
rom[header + 0x17] = 0x05   # 32 KB ROM
rom[header + 0x18] = 0x01   # 2 KB SRAM
rom[header + 0x19] = 0x01   # North America
for vector in range(0x7FE4, 0x7FF0, 2):
    put16(vector, rti)
for vector in range(0x7FF4, 0x8000, 2):
    put16(vector, rti)
put16(0x7FFC, 0x8000)       # reset
put16(0x7FDC, 0xFFFF)
put16(0x7FDE, 0x0000)
checksum = sum(rom) & 0xFFFF
put16(0x7FDE, checksum)
put16(0x7FDC, checksum ^ 0xFFFF)

with open(sys.argv[1], "wb") as f:
    f.write(rom)
