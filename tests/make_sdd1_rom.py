#!/usr/bin/env python3
"""Writes a 2 MB S-DD1 test cartridge (tests/run.sh plays it).

It checks what an S-DD1 game needs from the emulator:
  1. the bank registers $4804-$4807: which megabyte of the ROM banks c0-cf,
     d0-df, e0-ef and f0-ff show (read through them without decompressing);
  2. decompression: DMA channels whose bit is set in $4801, reading a fixed
     address in banks c0-ff, get the S-DD1's decompressed stream instead of
     the ROM's bytes. Eight streams (every bit plane mode and context mode)
     of different lengths, the last 65536 bytes (DMA size 0), go into WRAM
     through $2180 and are compared with the expected bytes, also in the ROM;
  3. $4801 is clear after a decompressing DMA;
  4. the banks stay as set (after a save state is loaded, see 4. below).
The backdrop turns green when everything passed, red when something failed
(and WRAM $0000 holds the test number: 1 for the banks, 2 + n for stream n,
0x20 + n when $4801 wasn't clear, 0x30 when the banks changed later, as
after a save state that didn't restore them).

The compressed streams are random bytes (any bytes decompress to
something); the expected bytes come from the S-DD1 decompressor below, an
implementation of Andreas Naive's description of the algorithm written for
this test, separate from the emulator's (pocketsnes/snes9x/sdd1emu.cpp).
"""
import random
import sys

# --- The S-DD1 decompressor -------------------------------------------------

# (Golomb code order, next state after a run of most probable bits, next
# state after a least probable bit) for the 33 probability states
EVOLUTION = [
    (0, 25, 25), (0, 2, 1), (0, 3, 1), (0, 4, 2), (0, 5, 3), (1, 6, 4), (1, 7, 5),
    (1, 8, 6), (1, 9, 7), (2, 10, 8), (2, 11, 9), (2, 12, 10), (2, 13, 11), (3, 14, 12),
    (3, 15, 13), (3, 16, 14), (3, 17, 15), (4, 18, 16), (4, 19, 17), (5, 20, 18),
    (5, 21, 19), (6, 22, 20), (6, 23, 21), (7, 24, 22), (7, 24, 23), (0, 26, 1),
    (1, 27, 2), (2, 28, 4), (3, 29, 8), (4, 30, 12), (5, 31, 16), (6, 32, 18), (7, 24, 22),
]


def run_length(index):
    """Most probable bits before the least probable one, for a code word
    whose remaining bits (after its leading 1) are those of index, read in
    reverse order."""
    if index < 2:
        return 0
    k = index.bit_length() - 1
    low = ~index & ((1 << k) - 1)
    return int(format(low, "0%db" % k)[::-1], 2)


def sdd1_decompress(rom, start, length):
    """The first length bytes the S-DD1 outputs for the stream at rom[start]."""
    pos = start
    bit_count = 4

    def code_word(order):
        nonlocal pos, bit_count
        word = (rom[pos] << bit_count) & 0xFF
        bit_count += 1
        if word & 0x80:
            word |= rom[pos + 1] >> (9 - bit_count)
            bit_count += order
        if bit_count & 8:
            pos += 1
            bit_count &= 7
        return word & 0xFF

    mps_run = [0] * 8   # most probable bits left in the current run, per code order
    lps_next = [0] * 8  # a least probable bit ends the run

    def golomb_bit(order):
        """(bit, whether the run ended) from the order's Golomb decoder"""
        if not (mps_run[order] or lps_next[order]):
            word = code_word(order)
            if word & 0x80:
                lps_next[order] = 1
                mps_run[order] = run_length(word >> (order ^ 7))
            else:
                mps_run[order] = 1 << order
        if mps_run[order]:
            bit = 0
            mps_run[order] -= 1
        else:
            bit = 1
            lps_next[order] = 0
        return bit, not (mps_run[order] or lps_next[order])

    state = [0] * 32
    mps = [0] * 32

    def probable_bit(context):
        st, cur_mps = state[context], mps[context]
        order, after_mps, after_lps = EVOLUTION[st]
        bit, run_ended = golomb_bit(order)
        if run_ended:
            if bit:
                if st < 2:
                    mps[context] ^= 1
                state[context] = after_lps
            else:
                state[context] = after_mps
        return bit ^ cur_mps

    header = rom[start]
    planes = header & 0xC0
    context_mode = header & 0x30
    history = [0] * 8   # each bit plane's last bits
    bit_number = 0
    plane = {0x00: 1, 0x40: 7, 0x80: 3, 0xC0: 0}[planes]

    def model_bit():
        nonlocal bit_number, plane
        if planes == 0x00:
            plane ^= 1
        elif planes == 0x40:
            plane ^= 1
            if not (bit_number & 0x7F):
                plane = (plane + 2) & 7
        elif planes == 0x80:
            plane ^= 1
            if not (bit_number & 0x7F):
                plane ^= 2
        else:
            plane = bit_number & 7
        h = history[plane]
        context = (plane & 1) << 4
        if context_mode == 0x00:
            context |= ((h & 0x1C0) >> 5) | (h & 1)
        elif context_mode == 0x10:
            context |= ((h & 0x180) >> 5) | (h & 1)
        elif context_mode == 0x20:
            context |= ((h & 0x0C0) >> 5) | (h & 1)
        else:
            context |= ((h & 0x180) >> 5) | (h & 3)
        bit = probable_bit(context)
        history[plane] = ((h << 1) | bit) & 0xFFFF
        bit_number = (bit_number + 1) & 0xFF
        return bit

    out = bytearray()
    if planes == 0xC0:
        while len(out) < length:
            value = 0
            for b in range(8):
                if model_bit():
                    value |= 1 << b
            out.append(value)
    else:
        while len(out) < length:
            first = second = 0
            for b in range(7, -1, -1):
                if model_bit():
                    first |= 1 << b
                if model_bit():
                    second |= 1 << b
            out.append(first)
            out.append(second)
    return bytes(out[:length])


# --- A few 65816 instructions ------------------------------------------------

class Asm:
    def __init__(self, origin):
        self.code = bytearray()
        self.origin = origin
        self.labels = {}
        self.fixups = []

    def here(self):
        return self.origin + len(self.code)

    def label(self, name):
        self.labels[name] = self.here()

    def emit(self, *bs):
        self.code += bytes(bs)

    def branch(self, opcode, name):
        self.emit(opcode, 0)
        self.fixups.append((len(self.code) - 1, name, "rel"))

    def bne_far(self, name):
        """bne to anywhere: beq over a jmp"""
        self.emit(0xF0, 3)
        self.jmp(name)

    def jmp(self, name):
        self.emit(0x4C, 0, 0)
        self.fixups.append((len(self.code) - 2, name, "abs"))

    def finish(self):
        for at, name, kind in self.fixups:
            target = self.labels[name]
            if kind == "rel":
                off = target - (self.origin + at + 1)
                assert -128 <= off < 128, name
                self.code[at] = off & 0xFF
            else:
                self.code[at] = target & 0xFF
                self.code[at + 1] = (target >> 8) & 0xFF
        return bytes(self.code)

    # (A is 8 bits, X and Y 16 bits)
    def lda_imm(self, v): self.emit(0xA9, v)
    def sta_abs(self, a): self.emit(0x8D, a & 0xFF, a >> 8)
    def stz_abs(self, a): self.emit(0x9C, a & 0xFF, a >> 8)
    def lda_abs(self, a): self.emit(0xAD, a & 0xFF, a >> 8)
    def lda_long(self, a): self.emit(0xAF, a & 0xFF, (a >> 8) & 0xFF, a >> 16)
    def lda_long_x(self, a): self.emit(0xBF, a & 0xFF, (a >> 8) & 0xFF, a >> 16)
    def cmp_imm(self, v): self.emit(0xC9, v)
    def cmp_long_x(self, a): self.emit(0xDF, a & 0xFF, (a >> 8) & 0xFF, a >> 16)
    def ldx_imm(self, v): self.emit(0xA2, v & 0xFF, v >> 8)
    def stx_abs(self, a): self.emit(0x8E, a & 0xFF, a >> 8)
    def cpx_imm(self, v): self.emit(0xE0, v & 0xFF, v >> 8)
    def inx(self): self.emit(0xE8)
    def sta_dp(self, d): self.emit(0x85, d)


# --- The cartridge ------------------------------------------------------------

ROM_SIZE = 0x200000
MARK0, MARK1 = 0x5A, 0xC3          # at 0x0F0000 (megabyte 0) and 0x1F0000 (megabyte 1)
STREAMS_AT = 0x010000              # compressed streams, megabyte 0 (banks c0-cf)
EXPECTED_AT = 0x100000             # what they decompress to, megabyte 1 (banks d0-df)


def build():
    rng = random.Random(20261013)
    rom = bytearray(rng.getrandbits(8) for _ in range(ROM_SIZE))
    rom[0x0F0000] = MARK0
    rom[0x1F0000] = MARK1

    # Every bit plane mode with every context mode at least once
    headers = [0x00, 0x50, 0xA0, 0xF0, 0x10, 0x60, 0xB0, 0xC0]
    lengths = [1, 37, 512, 2049, 7, 1000, 4096, 0x10000]
    streams = []
    # (all of them fit in megabyte 1, below its marker)
    assert EXPECTED_AT + sum(lengths) < 0x1F0000
    src = STREAMS_AT
    dst = EXPECTED_AT
    for n, (header, length) in enumerate(zip(headers, lengths)):
        rom[src] = header | (rom[src] & 0x0F)
        expected = sdd1_decompress(rom, src, length)
        rom[dst:dst + length] = expected
        streams.append((src, length, dst))
        src += length + 0x100              # a stream reads at most about as many bytes as it makes
        dst += length

    a = Asm(0x8000)
    a.emit(0x78, 0x18, 0xFB)              # sei; clc; xce
    a.emit(0xC2, 0x38)                    # rep #$38 (16-bit A, X, Y; binary)
    a.ldx_imm(0x1FFF); a.emit(0x9A)       # ldx #$1fff; txs
    a.emit(0xA9, 0x00, 0x00, 0x5B)        # lda #$0000; tcd
    a.emit(0xE2, 0x20)                    # sep #$20 (8-bit A)
    a.emit(0xA9, 0x00, 0x48, 0xAB)        # lda #0; pha; plb
    a.lda_imm(0x80); a.sta_abs(0x2100)    # forced blank
    a.stz_abs(0x4200)

    # 1. the bank registers
    a.lda_imm(1); a.sta_dp(0x00)
    a.lda_long(0xCF0000); a.cmp_imm(MARK0); a.bne_far("fail")     # c0-cf: megabyte 0
    a.lda_long(0xDF0000); a.cmp_imm(MARK1); a.bne_far("fail")     # d0-df: megabyte 1
    a.lda_imm(1); a.sta_abs(0x4804)
    a.lda_long(0xCF0000); a.cmp_imm(MARK1); a.bne_far("fail")
    a.lda_imm(0); a.sta_abs(0x4807)
    a.lda_long(0xFF0000); a.cmp_imm(MARK0); a.bne_far("fail")
    a.lda_imm(0); a.sta_abs(0x4804)
    a.lda_imm(3); a.sta_abs(0x4807)

    # 2. the streams, through DMA channel 0 into WRAM 7f0000
    for n, (src, length, dst) in enumerate(streams):
        a.lda_imm(2 + n); a.sta_dp(0x00)
        a.stz_abs(0x2181); a.stz_abs(0x2182); a.lda_imm(1); a.sta_abs(0x2183)
        a.lda_imm(0x08); a.sta_abs(0x4300)            # A fixed, one byte to one register
        a.lda_imm(0x80); a.sta_abs(0x4301)            # $2180
        address = 0xC00000 + src                      # banks c0-cf show megabyte 0
        a.ldx_imm(address & 0xFFFF); a.stx_abs(0x4302)
        a.lda_imm(address >> 16); a.sta_abs(0x4304)
        a.ldx_imm(length & 0xFFFF); a.stx_abs(0x4305) # (0 = 65536)
        a.lda_imm(1); a.sta_abs(0x4800); a.sta_abs(0x4801)
        a.lda_imm(1); a.sta_abs(0x420B)
        a.lda_imm(0x20 + n); a.sta_dp(0x00)
        a.lda_abs(0x4801); a.bne_far("fail")     # cleared after the DMA
        a.lda_imm(2 + n); a.sta_dp(0x00)
        expected = 0xD00000 + (dst - 0x100000)        # banks d0-df show megabyte 1
        a.ldx_imm(0)
        a.label("cmp%d" % n)
        a.lda_long_x(0x7F0000); a.cmp_long_x(expected); a.bne_far("fail")
        a.inx()
        if length == 0x10000:
            a.branch(0xD0, "cmp%d" % n)               # X wraps to 0 after 65536
        else:
            a.cpx_imm(length); a.branch(0xD0, "cmp%d" % n)

    # green backdrop: passed
    a.stz_abs(0x2121); a.lda_imm(0xE0); a.sta_abs(0x2122); a.lda_imm(0x03); a.sta_abs(0x2122)
    a.lda_imm(0x0F); a.sta_abs(0x2100)
    # 4. Then d0-df show megabyte 0, and that's checked over and over: after
    # a save state is loaded on a fresh start (where d0-df show megabyte 1),
    # it only stays green if the loading put the banks back.
    a.lda_imm(0); a.sta_abs(0x4805)
    a.lda_imm(0x30); a.sta_dp(0x00)
    a.label("pass")
    a.lda_long(0xDF0000); a.cmp_imm(MARK0); a.bne_far("fail")
    a.jmp("pass")
    # red backdrop: failed
    a.label("fail")
    a.stz_abs(0x2121); a.lda_imm(0x1F); a.sta_abs(0x2122); a.stz_abs(0x2122)
    a.lda_imm(0x0F); a.sta_abs(0x2100)
    a.label("stop"); a.branch(0x80, "stop")
    a.label("rti"); a.emit(0x40)
    code = a.finish()
    assert len(code) < 0x7FC0
    rom[0:len(code)] = code

    def put16(at, value):
        rom[at] = value & 0xFF
        rom[at + 1] = value >> 8

    header = 0x7FC0
    rom[header:header + 21] = b"SDD1 TEST".ljust(21)
    rom[0x7FD5] = 0x32   # LoROM, as Star Ocean and Street Fighter Alpha 2
    rom[0x7FD6] = 0x43   # ROM + S-DD1
    rom[0x7FD7] = 0x0B   # 2 MB
    rom[0x7FD8] = 0x00   # no SRAM
    rom[0x7FD9] = 0x01
    rom[0x7FDA] = 0x33
    rom[0x7FDB] = 0x00
    rti = a.labels["rti"]
    for vector in list(range(0x7FE4, 0x7FF0, 2)) + list(range(0x7FF4, 0x7FFC, 2)) + [0x7FFE]:
        put16(vector, rti)
    put16(0x7FFC, 0x8000)
    put16(0x7FDC, 0xFFFF)
    put16(0x7FDE, 0x0000)
    checksum = sum(rom) & 0xFFFF
    put16(0x7FDE, checksum)
    put16(0x7FDC, checksum ^ 0xFFFF)
    return bytes(rom)


if __name__ == "__main__":
    with open(sys.argv[1], "wb") as f:
        f.write(build())
