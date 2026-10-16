/* The Super FX (GSU) translated into ARM code, for the calculator build with
 * the ARM CPU core setting: fx_run (fxinst.cpp) calls fx_jit_run to run the
 * GSU until it stops.
 *
 * Code is translated in blocks: from an address, in a given prefix state
 * (the ALT mode, the B flag and the source and destination registers that
 * to/with/from/alt set), up to the first branch, jump or loop and its delay
 * slot. The prefixes cost nothing at run time: the block knows which
 * registers each instruction uses. R15 is known too (an instruction at
 * address A runs with R15 = A + 1), so only the exits store it. The
 * registers, flags and everything else stay in GSU (r4 points at it), and a
 * flag is only stored if something can still read it before the next
 * instruction that sets it (the end of a block reads them all). A block's
 * exit jumps straight into the block it leads to, once that one is
 * translated (the dispatcher patches the branch); loop jumps there when R13
 * is still what it was when the block was translated.
 *
 * Each instruction does exactly what the C interpreter does, the bits above
 * 16 that registers can hold included: tests/fx_test.cpp runs every opcode
 * and random programs on both and compares the whole GSU state and memory
 * (fx_jit_run_counted, which translates blocks that stop after a given
 * number of instructions, and fx_jit_run_until_stop), and tests/asm/run.sh
 * compares Yoshi's Island's frames with the C setting's. What isn't
 * translated runs on the C interpreter one instruction at a time (fx_step):
 * code in the GSU's RAM, the rare instructions that jump by writing R15,
 * ljmp, a delay slot holding a jump or a multi-byte instruction, an
 * instruction that reads R15 in a delay slot, the end of a bank, and a
 * state where the pipe doesn't hold the byte before R15 (a run's first
 * instruction, after a stop). */
#include "snes9x.h"
#include "fxemu.h"
#include "fxinst.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef ARM_ASM_FX

/* (fxinst.h's names for the GSU registers; R0-R12 here are ARM ones) */
#undef R0
#undef R1
#undef R2
#undef R3
#undef R4
#undef R5
#undef R6
#undef R7
#undef R8
#undef R9
#undef R10
#undef R11
#undef R12
#undef R13
#undef R14
#undef R15

extern struct FxRegs_s GSU;

/* ---- Translated code ---- */

#define CODE_WORDS (64 * 1024)		/* 256 KB */
uint32 fx_jit_code [CODE_WORDS] __attribute__ ((aligned (32)));
static uint32 *code_end;		/* where the next block goes */
static uint32 *enter_code;		/* (code, &GSU, fx_apfOpcodeTable): runs a block */
static uint32 *exit_code;		/* blocks end here, r0 = a branch to patch or 0 */
static bool out_of_room;

/* In counted runs (the tests): instructions run by the last block */
static uint32 counted_insns;

/* For the tests: instructions run translated (counted runs) and by the
 * interpreter, blocks translated; in fx_jit_run, blocks entered from the
 * dispatcher, instructions interpreted, branches patched */
uint32 fx_jit_stats [12];
/* (debugging: 6 translations, 7 failed ones, 8 exits to the dispatcher with
 * nothing to patch, 9 flushes, 10 steps of code in RAM, 11 steps because
 * the state wasn't translatable) */
#ifdef FX_JIT_STATS_PRINT
extern "C" int printf (const char *, ...);
/* (addresses: failed translations and where dispatcher exits went) */
#define LOG_SLOTS 64
static uint32 fail_key [LOG_SLOTS], fail_count [LOG_SLOTS], dyn_key [LOG_SLOTS], dyn_count [LOG_SLOTS];
static void log_key (uint32 *keys, uint32 *counts, uint32 key)
{
    for (int i = 0; i < LOG_SLOTS; i++)
    {
	if (counts [i] && keys [i] == key) { counts [i]++; return; }
	if (!counts [i]) { keys [i] = key; counts [i] = 1; return; }
    }
}
static void print_stats (void)
{
    for (int i = 0; i < LOG_SLOTS && fail_count [i]; i++)
	printf ("failed %02x:%04x st %03x: %u times, bytes %02x %02x %02x %02x\n", fail_key [i] >> 27,
		(fail_key [i] >> 11) & 0xffff, fail_key [i] & 0x7ff, fail_count [i],
		GSU.apvRomBank [fail_key [i] >> 27] [(fail_key [i] >> 11) & 0xffff],
		GSU.apvRomBank [fail_key [i] >> 27] [((fail_key [i] >> 11) + 1) & 0xffff],
		GSU.apvRomBank [fail_key [i] >> 27] [((fail_key [i] >> 11) + 2) & 0xffff],
		GSU.apvRomBank [fail_key [i] >> 27] [((fail_key [i] >> 11) + 3) & 0xffff]);
    for (int i = 0; i < LOG_SLOTS && dyn_count [i]; i++)
	printf ("dispatcher exit to %02x:%04x st %03x: %u times\n", dyn_key [i] >> 27, (dyn_key [i] >> 11) & 0xffff,
		dyn_key [i] & 0x7ff, dyn_count [i]);
    printf ("fx_jit: entered %u, translated %u (failed %u), interpreted %u (not translatable %u, RAM %u), "
	    "linked %u, unpatched exits %u, flushes %u\n", fx_jit_stats [3], fx_jit_stats [6], fx_jit_stats [7],
	    fx_jit_stats [4], fx_jit_stats [11], fx_jit_stats [10], fx_jit_stats [5], fx_jit_stats [8],
	    fx_jit_stats [9]);
}
#endif

/* Blocks by GSU address and prefix state */
#define BLOCKS 4096
struct Block
{
    uint32 key_high, key_low;	/* key_high 0: empty */
    uint32 *code;
};
static Block blocks [BLOCKS];
static int block_count;
static uint32 flushes;			/* (a branch to patch is in code that is still there) */

/* The prefix state an instruction runs in */
struct State
{
    uint8 alt, b, s, d;
};

static inline bool same_state (State a, State b)
{
    return a.alt == b.alt && a.b == b.b && a.s == b.s && a.d == b.d;
}

static inline uint32 state_bits (State st)
{
    return st.alt | st.b << 2 | st.s << 3 | st.d << 7;
}

static uint32 *find_block (uint32 pbr, uint32 addr, State st)
{
    uint32 high = 0x80000000u | pbr, low = addr << 11 | state_bits (st);
    uint32 h = (low * 2654435761u ^ pbr * 40503u) >> 20;
    for (int i = 0; i < BLOCKS; i++, h = (h + 1) & (BLOCKS - 1))
    {
	if (!blocks [h].key_high)
	    return NULL;
	if (blocks [h].key_high == high && blocks [h].key_low == low)
	    return blocks [h].code;
    }
    return NULL;
}

static bool add_block (uint32 pbr, uint32 addr, State st, uint32 *code)
{
    if (block_count >= BLOCKS / 2)
	return false;
    uint32 high = 0x80000000u | pbr, low = addr << 11 | state_bits (st);
    uint32 h = (low * 2654435761u ^ pbr * 40503u) >> 20;
    while (blocks [h].key_high)
	h = (h + 1) & (BLOCKS - 1);
    blocks [h].key_high = high;
    blocks [h].key_low = low;
    blocks [h].code = code;
    block_count++;
    return true;
}

/* ---- ARM code ---- */

enum { R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R_SP, R_LR, R_PC };
enum { C_EQ, C_NE, C_CS, C_CC, C_MI, C_PL, C_VS, C_VC, C_HI, C_LS, C_GE, C_LT, C_GT, C_LE, C_AL };
enum { OP_AND, OP_EOR, OP_SUB, OP_RSB, OP_ADD, OP_ADC, OP_SBC, OP_RSC, OP_TST, OP_TEQ, OP_CMP, OP_CMN, OP_ORR, OP_MOV, OP_BIC, OP_MVN };
enum { SH_LSL, SH_LSR, SH_ASR, SH_ROR };

static uint32 *out;			/* the next instruction */
static uint32 *out_limit;

static inline void put (uint32 word)
{
    if (out < out_limit)
	*out++ = word;
    else
	out_of_room = true;
}

/* The operand 2 encoding of an immediate, or -1 */
static int imm_bits (uint32 v)
{
    for (int rot = 0; rot < 16; rot++)
    {
	uint32 r = rot ? (v << (2 * rot)) | (v >> (32 - 2 * rot)) : v;
	if (r < 256)
	    return rot << 8 | r;
    }
    return -1;
}

static bool bad_code;			/* an immediate that can't be encoded: don't use the block */

/* op rd, rn, #imm; orr, bic and eor take any imm (a byte at a time) */
static void dp_i (int op, int rd, int rn, uint32 imm, int cond = C_AL, int s = 0)
{
    int bits = imm_bits (imm);
    if (bits < 0 && (op == OP_ORR || op == OP_BIC || op == OP_EOR) && !s)
    {
	for (int sh = 0; sh < 32; sh += 8)
	    if (imm & (0xffu << sh))
	    {
		dp_i (op, rd, rn, imm & (0xffu << sh), cond);
		rn = rd;
	    }
	return;
    }
    if (bits < 0)
    {
	bad_code = true;
	bits = 0;
    }
    put ((uint32) cond << 28 | 1u << 25 | op << 21 | s << 20 | rn << 16 | rd << 12 | bits);
}

/* op rd, rn, rm, shift #amount */
static void dp_r (int op, int rd, int rn, int rm, int shift = SH_LSL, int amount = 0, int cond = C_AL, int s = 0)
{
    put ((uint32) cond << 28 | op << 21 | s << 20 | rn << 16 | rd << 12 | (amount & 31) << 7 | shift << 5 | rm);
}

/* rd = v, in 1 to 4 instructions */
static void mov_const (int rd, uint32 v, int cond = C_AL)
{
    if (imm_bits (v) >= 0)
    {
	dp_i (OP_MOV, rd, 0, v, cond);
	return;
    }
    if (imm_bits (~v) >= 0)
    {
	dp_i (OP_MVN, rd, 0, ~v, cond);
	return;
    }
    bool first = true;
    for (int sh = 0; sh < 32; sh += 8)
	if (v & (0xffu << sh))
	{
	    dp_i (first ? OP_MOV : OP_ORR, rd, first ? 0 : rd, v & (0xffu << sh), cond);
	    first = false;
	}
}

/* ldr/str (byte) rt, [rn, #off] */
static void mem_i (bool load, bool byte, int rt, int rn, int off, int cond = C_AL)
{
    uint32 u = off >= 0, a = off >= 0 ? off : -off;
    put ((uint32) cond << 28 | 1u << 26 | 1u << 24 | u << 23 | (uint32) byte << 22 | (uint32) load << 20 |
	 rn << 16 | rt << 12 | a);
}
#define LDR(rt, rn, off, ...)  mem_i (true, false, rt, rn, off, ##__VA_ARGS__)
#define STR(rt, rn, off, ...)  mem_i (false, false, rt, rn, off, ##__VA_ARGS__)
#define LDRB(rt, rn, off, ...) mem_i (true, true, rt, rn, off, ##__VA_ARGS__)
#define STRB(rt, rn, off, ...) mem_i (false, true, rt, rn, off, ##__VA_ARGS__)

/* ldrb/strb rt, [rn, rm, shift #amount] */
static void mem_r (bool load, int rt, int rn, int rm, int shift, int amount)
{
    put (0xE7C00000u | (uint32) load << 20 | rn << 16 | rt << 12 | (amount & 31) << 7 | shift << 5 | rm);
}

/* mul rd, rm, rs (rd != rm) */
static void mul (int rd, int rm, int rs)
{
    put (0xE0000090u | rd << 16 | rs << 8 | rm);
}

static uint32 branch_word (const uint32 *at, const uint32 *target, int cond, bool link = false)
{
    int32 offset = (int32) (target - (at + 2));
    return (uint32) cond << 28 | 0x0A000000u | (uint32) link << 24 | ((uint32) offset & 0xffffff);
}

static void b_to (const uint32 *target, int cond = C_AL)
{
    put (branch_word (out, target, cond));
}

static void blx (int rm)
{
    put (0xE12FFF30u | rm);
}

/* ---- GSU fields ---- */

#define G(field) ((int) offsetof (struct FxRegs_s, field))
#define REG(n)   (4 * (n))
#define F_S 1		/* vSign */
#define F_Z 2		/* vZero */
#define F_C 4		/* vCarry */
#define F_V 8		/* vOverflow */
#define F_ALL 15

/* Code bytes at translation time: the program bank */
static uint8 *prg;
static uint32 code_pbr;

static inline uint8 code_byte (uint32 addr)
{
    return prg [addr & 0xffff];
}

/* ---- Decoding ---- */

enum Kind
{
    K_PREFIX,		/* to/with/from/alt: only the state changes */
    K_STOP, K_NOP, K_HELPER, K_PLOT,
    K_LSR, K_ROL, K_MOVE, K_MOVES, K_STW, K_STB, K_LDW, K_LDB,
    K_SWAP, K_COLOR, K_NOT, K_ADD, K_ADC, K_ADD_I, K_ADC_I, K_SUB, K_SBC, K_SUB_I, K_CMP,
    K_MERGE, K_AND, K_BIC, K_AND_I, K_BIC_I, K_MULT, K_UMULT, K_MULT_I, K_UMULT_I,
    K_SBK, K_LINK, K_SEX, K_LOB, K_HIB, K_OR, K_XOR, K_OR_I, K_XOR_I,
    K_INC, K_DEC, K_GETC, K_GETB, K_GETBH, K_GETBL, K_GETBS,
    K_IBT, K_LMS, K_SMS, K_IWT, K_LM, K_SM,
    /* control: a delay slot follows */
    K_BRA, K_BCOND, K_LOOP, K_JMP, K_IWT_R15,
    K_UNSUPPORTED	/* runs on the interpreter */
};

struct Insn
{
    uint32 addr;
    uint8 op, kind, len;
    State st;		/* the state it runs in */
    uint32 imm;		/* operand bytes */
    uint8 reads, writes, store;	/* flags */
};

/* What reads a register argument n (op & 15) */
static inline bool control (int kind)
{
    return kind >= K_BRA && kind <= K_IWT_R15;
}

/* Decodes the instruction at addr in state st. Returns false for a
 * prefix (st updated). */
static bool decode (uint32 addr, State &st, Insn &in)
{
    uint8 op = code_byte (addr);
    int n = op & 15;
    in.addr = addr;
    in.op = op;
    in.st = st;
    in.len = 1;
    in.imm = 0;
    in.reads = in.writes = 0;
    in.kind = K_UNSUPPORTED;
    bool reads_r15 = false, writes_dreg = false, reads_sreg = false;

    /* Prefixes */
    if (op >= 0x10 && op <= 0x1f && !st.b)
    {
	st.d = n;
	in.kind = K_PREFIX;
	return false;
    }
    if (op >= 0x20 && op <= 0x2f)
    {
	st.b = 1;
	st.s = st.d = n;
	in.kind = K_PREFIX;
	return false;
    }
    if (op >= 0xb0 && op <= 0xbf && !st.b)
    {
	st.s = n;
	in.kind = K_PREFIX;
	return false;
    }
    if (op >= 0x3d && op <= 0x3f)
    {
	st.alt |= op == 0x3d ? 1 : op == 0x3e ? 2 : 3;
	st.b = 0;
	in.kind = K_PREFIX;
	return false;
    }

    int alt = st.alt;
    switch (op)
    {
    case 0x00: in.kind = K_STOP; break;
    case 0x01: in.kind = K_NOP; break;
    case 0x02: in.kind = K_HELPER; reads_r15 = true; break;	/* cache */
    case 0x03: in.kind = K_LSR; reads_sreg = writes_dreg = true; in.writes = F_S | F_Z | F_C; break;
    case 0x04: in.kind = K_ROL; reads_sreg = writes_dreg = true; in.reads = F_C; in.writes = F_S | F_Z | F_C; break;
    case 0x05: in.kind = K_BRA; in.len = 2; break;
    case 0x3c: in.kind = K_LOOP; in.writes = F_S | F_Z; break;
    case 0x4c:
	in.kind = alt & 1 ? K_HELPER : K_PLOT;	/* rpix / plot */
	if (alt & 1)
	    writes_dreg = true;
	break;
    case 0x4d: in.kind = K_SWAP; reads_sreg = writes_dreg = true; in.writes = F_S | F_Z; break;
    case 0x4e:
	in.kind = alt & 1 ? K_HELPER : K_COLOR;	/* cmode / color */
	reads_sreg = true;
	break;
    case 0x4f: in.kind = K_NOT; reads_sreg = writes_dreg = true; in.writes = F_S | F_Z; break;
    case 0x70: in.kind = K_MERGE; writes_dreg = true; in.writes = F_ALL; break;
    case 0x90: in.kind = K_SBK; reads_sreg = true; break;
    case 0x95: in.kind = K_SEX; reads_sreg = writes_dreg = true; in.writes = F_S | F_Z; break;
    case 0x96: case 0x97: case 0x9f:	/* asr/div2, ror, fmult/lmult */
	in.kind = K_HELPER;
	reads_sreg = writes_dreg = true;
	break;
    case 0x9e: in.kind = K_LOB; reads_sreg = writes_dreg = true; in.writes = F_S | F_Z; break;
    case 0xc0: in.kind = K_HIB; reads_sreg = writes_dreg = true; in.writes = F_S | F_Z; break;
    case 0xdf:
	if (alt < 2)
	    in.kind = K_GETC;
	else
	{
	    in.kind = K_HELPER;		/* ramb / romb */
	    reads_sreg = true;
	}
	break;
    case 0xef:
	in.kind = alt == 0 ? K_GETB : alt == 1 ? K_GETBH : alt == 2 ? K_GETBL : K_GETBS;
	writes_dreg = true;
	reads_sreg = alt == 1 || alt == 2;
	break;
    default:
	break;
    }

    if (op >= 0x06 && op <= 0x0f)
    {
	in.kind = K_BCOND;
	in.len = 2;
	static const uint8 flags [10] = { F_S | F_V, F_S | F_V, F_Z, F_Z, F_S, F_S, F_C, F_C, F_V, F_V };
	in.reads = flags [op - 6];
    }
    else if (op >= 0x10 && op <= 0x1f)		/* move (B) */
    {
	in.kind = n == 15 ? K_UNSUPPORTED : K_MOVE;
	reads_sreg = true;
    }
    else if (op >= 0x30 && op <= 0x3b)
    {
	in.kind = alt & 1 ? K_STB : K_STW;
	reads_sreg = true;
    }
    else if (op >= 0x40 && op <= 0x4b)
    {
	in.kind = alt & 1 ? K_LDB : K_LDW;
	writes_dreg = true;
    }
    else if (op >= 0x50 && op <= 0x5f)
    {
	static const uint8 kinds [4] = { K_ADD, K_ADC, K_ADD_I, K_ADC_I };
	in.kind = kinds [alt];
	reads_sreg = writes_dreg = true;
	reads_r15 = alt < 2 && n == 15;
	in.reads = alt & 1 ? F_C : 0;
	in.writes = F_ALL;
    }
    else if (op >= 0x60 && op <= 0x6f)
    {
	static const uint8 kinds [4] = { K_SUB, K_SBC, K_SUB_I, K_CMP };
	in.kind = kinds [alt];
	reads_sreg = true;
	writes_dreg = alt != 3;
	reads_r15 = alt != 2 && n == 15;
	in.reads = alt == 1 ? F_C : 0;
	in.writes = F_ALL;
    }
    else if (op >= 0x71 && op <= 0x7f)
    {
	static const uint8 kinds [4] = { K_AND, K_BIC, K_AND_I, K_BIC_I };
	in.kind = kinds [alt];
	reads_sreg = writes_dreg = true;
	reads_r15 = alt < 2 && n == 15;
	in.writes = F_S | F_Z;
    }
    else if (op >= 0x80 && op <= 0x8f)
    {
	static const uint8 kinds [4] = { K_MULT, K_UMULT, K_MULT_I, K_UMULT_I };
	in.kind = kinds [alt];
	reads_sreg = writes_dreg = true;
	reads_r15 = alt < 2 && n == 15;
	in.writes = F_S | F_Z;
    }
    else if (op >= 0x91 && op <= 0x94)
    {
	in.kind = K_LINK;
	reads_r15 = true;
    }
    else if (op >= 0x98 && op <= 0x9d)
	in.kind = alt & 1 ? K_UNSUPPORTED : K_JMP;	/* ljmp / jmp */
    else if (op >= 0xa0 && op <= 0xaf)
    {
	in.len = 2;
	in.imm = code_byte (addr + 1);
	if (alt == 0)
	    in.kind = K_IBT;
	else if (alt == 2)
	    in.kind = K_SMS;
	else
	    in.kind = K_LMS;
	if (n == 15)
	{
	    if (alt == 2)
		reads_r15 = true;
	    else
		in.kind = K_UNSUPPORTED;	/* a jump */
	}
    }
    else if (op >= 0xb0 && op <= 0xbf)		/* moves (B) */
    {
	in.kind = K_MOVES;
	writes_dreg = true;
	reads_r15 = n == 15;
	in.writes = F_S | F_Z | F_V;
    }
    else if (op >= 0xc1 && op <= 0xcf)
    {
	static const uint8 kinds [4] = { K_OR, K_XOR, K_OR_I, K_XOR_I };
	in.kind = kinds [alt];
	reads_sreg = writes_dreg = true;
	reads_r15 = alt < 2 && n == 15;
	in.writes = F_S | F_Z;
    }
    else if (op >= 0xd0 && op <= 0xde)
    {
	in.kind = K_INC;
	in.writes = F_S | F_Z;
    }
    else if (op >= 0xe0 && op <= 0xee)
    {
	in.kind = K_DEC;
	in.writes = F_S | F_Z;
    }
    else if (op >= 0xf0)
    {
	in.len = 3;
	in.imm = code_byte (addr + 1) | code_byte (addr + 2) << 8;
	if (alt == 0)
	    in.kind = n == 15 ? K_IWT_R15 : K_IWT;
	else if (alt == 2)
	{
	    in.kind = K_SM;
	    reads_r15 = n == 15;
	}
	else
	    in.kind = n == 15 ? K_UNSUPPORTED : K_LM;
    }

    /* Source R15 reads A + 1; a destination of R15 is a jump. */
    if (reads_sreg && st.s == 15)
	reads_r15 = true;
    if (writes_dreg && st.d == 15)
	in.kind = K_UNSUPPORTED;
    if (in.kind == K_HELPER)
	in.reads = F_ALL;		/* (and its writes aren't counted on) */
    if (addr + in.len - 1 > 0xffff)
	in.kind = K_UNSUPPORTED;	/* the bank's end */
    /* reads_r15 is kept in reads' bit 7 */
    if (reads_r15)
	in.reads |= 0x80;
    return true;
}

/* A delay slot the blocks can run: one byte, no jump, R15 not read. */
static bool good_delay_slot (const Insn &in, bool prefix)
{
    if (prefix)
	return true;
    return in.len == 1 && !control (in.kind) && in.kind != K_UNSUPPORTED && in.kind != K_STOP &&
	!(in.reads & 0x80);
}

/* ---- Code for instructions ---- */

static void load_reg (int rt, int n, uint32 addr)
{
    if (n == 15)
	mov_const (rt, addr + 1);
    else
	LDR (rt, R4, REG (n));
}

/* vRomBuffer = ROM [R14] (uses r1, r2) */
static void read_r14 (void)
{
    LDR (R1, R4, G (pvRomBank));
    LDR (R2, R4, REG (14));
    dp_r (OP_MOV, R2, 0, R2, SH_LSL, 16);
    mem_r (true, R2, R1, R2, SH_LSR, 16);
    STRB (R2, R4, G (vRomBuffer));
}

/* r[d] = rv, then the ROM buffer if d is R14 */
static void set_dreg (int rv, int d)
{
    STR (rv, R4, REG (d));
    if (d == 14)
	read_r14 ();
}

static void store_sz (int rv, const Insn &in)
{
    if (in.store & F_S)
	STR (rv, R4, G (vSign));
    if (in.store & F_Z)
	STR (rv, R4, G (vZero));
}

/* vCarry = the condition (cs/cc pair given as conditions) */
static void store_carry (const Insn &in, int yes, int no)
{
    if (!(in.store & F_C))
	return;
    dp_i (OP_MOV, R3, 0, 0, no);
    dp_i (OP_MOV, R3, 0, 1, yes);
    STR (R3, R4, G (vCarry));
}

/* Address byte (lsl 16, lsr 16) of a RAM access: ldrb/strb rt, [ram, a & 0xffff] */
static void ram_access (bool load, int rt, int ram, int addr_reg, int tmp)
{
    dp_r (OP_MOV, tmp, 0, addr_reg, SH_LSL, 16);
    mem_r (load, rt, ram, tmp, SH_LSR, 16);
}

/* GSU.pvSreg / pvDreg for the state (uses r0) */
static void store_regs_pointers (State st)
{
    dp_i (OP_ADD, R0, R4, REG (st.s));
    STR (R0, R4, G (pvSreg));
    if (st.d != st.s)
	dp_i (OP_ADD, R0, R4, REG (st.d));
    STR (R0, R4, G (pvDreg));
}

/* The instruction through fx_apfOpcodeTable (r5 points at it) */
static void call_helper (const Insn &in)
{
    if (in.kind != K_PLOT)
    {
	store_regs_pointers (in.st);
	mov_const (R0, in.addr + 1);
	STR (R0, R4, REG (15));
    }
    LDR (R12, R5, 4 * ((in.st.alt << 8) | in.op));
    blx (R12);
}

static void emit_insn (const Insn &in)
{
    const int s = in.st.s, d = in.st.d, n = in.op & 15;
    const uint32 a = in.addr;

    switch (in.kind)
    {
    case K_NOP:
	break;
    case K_HELPER:
    case K_PLOT:
	call_helper (in);
	break;
    case K_LSR:
	load_reg (R0, s, a);
	if (in.store & F_C)
	{
	    dp_i (OP_AND, R3, R0, 1);
	    STR (R3, R4, G (vCarry));
	}
	dp_r (OP_MOV, R2, 0, R0, SH_LSL, 16);
	dp_r (OP_MOV, R2, 0, R2, SH_LSR, 17);
	store_sz (R2, in);
	set_dreg (R2, d);
	break;
    case K_ROL:
	/* v = USEX16 ((SREG << 1) + vCarry); vCarry = (SREG >> 15) & 1 */
	load_reg (R0, s, a);
	LDR (R1, R4, G (vCarry));
	dp_r (OP_ADD, R2, R1, R0, SH_LSL, 1);
	dp_r (OP_MOV, R2, 0, R2, SH_LSL, 16);
	dp_r (OP_MOV, R2, 0, R2, SH_LSR, 16);
	if (in.store & F_C)
	{
	    dp_r (OP_MOV, R3, 0, R0, SH_LSR, 15);
	    dp_i (OP_AND, R3, R3, 1);
	    STR (R3, R4, G (vCarry));
	}
	store_sz (R2, in);
	set_dreg (R2, d);
	break;
    case K_MOVE:		/* to rn with B: r[n] = r[s] */
	load_reg (R0, s, a);
	set_dreg (R0, n);
	break;
    case K_MOVES:		/* from rn with B: r[d] = r[n] */
	load_reg (R0, n, a);
	if (in.store & F_V)
	{
	    dp_i (OP_AND, R1, R0, 0x80);
	    dp_r (OP_MOV, R1, 0, R1, SH_LSL, 16);
	    STR (R1, R4, G (vOverflow));
	}
	store_sz (R0, in);
	set_dreg (R0, d);
	break;
    case K_STW:
    case K_STB:
	/* vLastRamAdr = r[n]; RAM [a] = s (and RAM [a ^ 1] = s >> 8) */
	LDR (R0, R4, REG (n));
	STR (R0, R4, G (vLastRamAdr));
	load_reg (R1, s, a);
	LDR (R2, R4, G (pvRamBank));
	ram_access (false, R1, R2, R0, R3);
	if (in.kind == K_STW)
	{
	    dp_i (OP_EOR, R0, R0, 1);
	    dp_r (OP_MOV, R1, 0, R1, SH_LSR, 8);
	    ram_access (false, R1, R2, R0, R3);
	}
	break;
    case K_LDW:
    case K_LDB:
	LDR (R0, R4, REG (n));
	STR (R0, R4, G (vLastRamAdr));
	LDR (R2, R4, G (pvRamBank));
	ram_access (true, R1, R2, R0, R3);
	if (in.kind == K_LDW)
	{
	    dp_i (OP_EOR, R0, R0, 1);
	    ram_access (true, R3, R2, R0, R3);
	    dp_r (OP_ORR, R1, R1, R3, SH_LSL, 8);
	}
	set_dreg (R1, d);
	break;
    case K_SWAP:
	load_reg (R0, s, a);
	dp_i (OP_AND, R2, R0, 0xff);
	dp_r (OP_MOV, R1, 0, R0, SH_LSR, 8);
	dp_i (OP_AND, R1, R1, 0xff);
	dp_r (OP_ORR, R2, R1, R2, SH_LSL, 8);
	store_sz (R2, in);
	set_dreg (R2, d);
	break;
    case K_COLOR:
    case K_GETC:
	/* c = (uint8) SREG (color) or the ROM buffer (getc); POR bit 2: the
	 * high nibble into the low one too; bit 3: only the low nibble */
	if (in.kind == K_COLOR)
	{
	    load_reg (R0, s, a);
	    dp_i (OP_AND, R0, R0, 0xff);
	}
	else
	    LDRB (R0, R4, G (vRomBuffer));
	LDR (R1, R4, G (vPlotOptionReg));
	dp_i (OP_TST, 0, R1, 4, C_AL, 1);
	dp_i (OP_AND, R2, R0, 0xf0, C_NE);
	dp_r (OP_ORR, R0, R2, R0, SH_LSR, 4, C_NE);
	dp_i (OP_TST, 0, R1, 8, C_AL, 1);
	LDR (R2, R4, G (vColorReg), C_NE);
	dp_i (OP_AND, R2, R2, 0xf0, C_NE);
	dp_i (OP_AND, R0, R0, 0x0f, C_NE);
	dp_r (OP_ORR, R0, R2, R0, SH_LSL, 0, C_NE);
	STR (R0, R4, G (vColorReg));
	break;
    case K_NOT:
	load_reg (R0, s, a);
	dp_r (OP_MVN, R2, 0, R0);
	store_sz (R2, in);
	set_dreg (R2, d);
	break;
    case K_ADD:
    case K_ADD_I:
	/* s = USEX16 (SREG) + USEX16 (b): the carry out of 16 bits */
	load_reg (R0, s, a);
	if (in.kind == K_ADD)
	    load_reg (R1, n, a);
	else
	    dp_i (OP_MOV, R1, 0, n);
	dp_r (OP_MOV, R2, 0, R0, SH_LSL, 16);
	dp_r (OP_ADD, R2, R2, R1, SH_LSL, 16, C_AL, 1);
	dp_r (OP_MOV, R2, 0, R2, SH_LSR, 16);
	dp_i (OP_ORR, R2, R2, 0x10000, C_CS);
	store_carry (in, C_CS, C_CC);
	goto add_overflow;
    case K_ADC:
    case K_ADC_I:
	/* adc: + SEX16 (vCarry); adc #n: + USEX16 (vCarry) */
	load_reg (R0, s, a);
	if (in.kind == K_ADC)
	    load_reg (R1, n, a);
	else
	    dp_i (OP_MOV, R1, 0, n);
	dp_r (OP_MOV, R2, 0, R0, SH_LSL, 16);
	dp_r (OP_MOV, R2, 0, R2, SH_LSR, 16);
	if (in.kind == K_ADC)
	{
	    dp_r (OP_MOV, R3, 0, R1, SH_LSL, 16);
	    dp_r (OP_ADD, R2, R2, R3, SH_LSR, 16);
	}
	else
	    dp_r (OP_ADD, R2, R2, R1);
	LDR (R3, R4, G (vCarry));
	dp_r (OP_MOV, R3, 0, R3, SH_LSL, 16);
	dp_r (OP_ADD, R2, R2, R3, in.kind == K_ADC ? SH_ASR : SH_LSR, 16);
	if (in.store & F_C)
	{
	    dp_i (OP_CMP, 0, R2, 0x10000, C_AL, 1);
	    store_carry (in, C_GE, C_LT);
	}
    add_overflow:
	/* vOverflow = ~(SREG ^ b) & (b ^ s) & 0x8000 */
	if (in.store & F_V)
	{
	    dp_r (OP_EOR, R3, R0, R1);
	    dp_r (OP_EOR, R12, R1, R2);
	    dp_r (OP_BIC, R12, R12, R3);
	    dp_i (OP_AND, R12, R12, 0x8000);
	    STR (R12, R4, G (vOverflow));
	}
	store_sz (R2, in);
	set_dreg (R2, d);
	break;
    case K_SUB:
    case K_SBC:
    case K_SUB_I:
    case K_CMP:
	/* s = USEX16 (SREG) - USEX16 (b) (- USEX16 (vCarry ^ 1)); vCarry = s >= 0 */
	load_reg (R0, s, a);
	if (in.kind == K_SUB_I)
	    dp_i (OP_MOV, R1, 0, n);
	else
	    load_reg (R1, n, a);
	dp_r (OP_MOV, R2, 0, R0, SH_LSL, 16);
	dp_r (OP_MOV, R2, 0, R2, SH_LSR, 16);
	if (in.kind == K_SUB_I)
	    dp_r (OP_SUB, R2, R2, R1);
	else
	{
	    dp_r (OP_MOV, R3, 0, R1, SH_LSL, 16);
	    dp_r (OP_SUB, R2, R2, R3, SH_LSR, 16);
	}
	if (in.kind == K_SBC)
	{
	    LDR (R3, R4, G (vCarry));
	    dp_i (OP_EOR, R3, R3, 1);
	    dp_r (OP_MOV, R3, 0, R3, SH_LSL, 16);
	    dp_r (OP_SUB, R2, R2, R3, SH_LSR, 16);
	}
	if (in.store & F_C)
	{
	    dp_i (OP_CMP, 0, R2, 0, C_AL, 1);
	    store_carry (in, C_GE, C_LT);
	}
	/* vOverflow = (SREG ^ b) & (SREG ^ s) & 0x8000 */
	if (in.store & F_V)
	{
	    dp_r (OP_EOR, R3, R0, R1);
	    dp_r (OP_EOR, R12, R0, R2);
	    dp_r (OP_AND, R12, R12, R3);
	    dp_i (OP_AND, R12, R12, 0x8000);
	    STR (R12, R4, G (vOverflow));
	}
	store_sz (R2, in);
	if (in.kind != K_CMP)
	    set_dreg (R2, d);
	break;
    case K_MERGE:
	/* v = (R7 & 0xff00) | ((R8 & 0xff00) >> 8) */
	LDR (R0, R4, REG (7));
	LDR (R1, R4, REG (8));
	dp_i (OP_AND, R2, R0, 0xff00);
	dp_r (OP_MOV, R1, 0, R1, SH_LSR, 8);
	dp_i (OP_AND, R1, R1, 0xff);
	dp_r (OP_ORR, R2, R2, R1);
	if (in.store & F_V)	/* (v & 0xc0c0) << 16 */
	{
	    mov_const (R3, 0xc0c0);
	    dp_r (OP_AND, R3, R2, R3);
	    dp_r (OP_MOV, R3, 0, R3, SH_LSL, 16);
	    STR (R3, R4, G (vOverflow));
	}
	if (in.store & F_Z)	/* !(v & 0xf0f0) */
	{
	    mov_const (R3, 0xf0f0);
	    dp_r (OP_TST, 0, R2, R3, SH_LSL, 0, C_AL, 1);
	    dp_i (OP_MOV, R3, 0, 1, C_EQ);
	    dp_i (OP_MOV, R3, 0, 0, C_NE);
	    STR (R3, R4, G (vZero));
	}
	if (in.store & F_S)	/* (v | (v << 8)) & 0x8000 */
	{
	    dp_r (OP_ORR, R3, R2, R2, SH_LSL, 8);
	    dp_i (OP_AND, R3, R3, 0x8000);
	    STR (R3, R4, G (vSign));
	}
	if (in.store & F_C)	/* (v & 0xe0e0) != 0 */
	{
	    mov_const (R3, 0xe0e0);
	    dp_r (OP_TST, 0, R2, R3, SH_LSL, 0, C_AL, 1);
	    dp_i (OP_MOV, R3, 0, 0, C_EQ);
	    dp_i (OP_MOV, R3, 0, 1, C_NE);
	    STR (R3, R4, G (vCarry));
	}
	set_dreg (R2, d);
	break;
    case K_AND: case K_BIC: case K_AND_I: case K_BIC_I:
    case K_OR: case K_XOR: case K_OR_I: case K_XOR_I:
	load_reg (R0, s, a);
	if (in.kind == K_AND_I || in.kind == K_BIC_I || in.kind == K_OR_I || in.kind == K_XOR_I)
	    dp_i (OP_MOV, R1, 0, n);
	else
	    load_reg (R1, n, a);
	dp_r (in.kind == K_AND || in.kind == K_AND_I ? OP_AND : in.kind == K_BIC || in.kind == K_BIC_I ? OP_BIC :
	      in.kind == K_OR || in.kind == K_OR_I ? OP_ORR : OP_EOR, R2, R0, R1);
	store_sz (R2, in);
	set_dreg (R2, d);
	break;
    case K_MULT: case K_UMULT: case K_MULT_I: case K_UMULT_I:
	/* (u)mult: SEX8/USEX8 of both; #n: SEX8/USEX8 (SREG) * n */
	load_reg (R0, s, a);
	if (in.kind == K_MULT || in.kind == K_UMULT)
	    load_reg (R1, n, a);
	else
	    dp_i (OP_MOV, R1, 0, n);
	if (in.kind == K_MULT || in.kind == K_MULT_I)
	{
	    dp_r (OP_MOV, R0, 0, R0, SH_LSL, 24);
	    dp_r (OP_MOV, R0, 0, R0, SH_ASR, 24);
	    if (in.kind == K_MULT)
	    {
		dp_r (OP_MOV, R1, 0, R1, SH_LSL, 24);
		dp_r (OP_MOV, R1, 0, R1, SH_ASR, 24);
	    }
	}
	else
	{
	    dp_i (OP_AND, R0, R0, 0xff);
	    if (in.kind == K_UMULT)
		dp_i (OP_AND, R1, R1, 0xff);
	}
	mul (R2, R0, R1);
	store_sz (R2, in);
	set_dreg (R2, d);
	break;
    case K_SBK:
	/* RAM [vLastRamAdr] = SREG, RAM [vLastRamAdr ^ 1] = SREG >> 8 */
	LDR (R0, R4, G (vLastRamAdr));
	load_reg (R1, s, a);
	LDR (R2, R4, G (pvRamBank));
	ram_access (false, R1, R2, R0, R3);
	dp_i (OP_EOR, R0, R0, 1);
	dp_r (OP_MOV, R1, 0, R1, SH_LSR, 8);
	ram_access (false, R1, R2, R0, R3);
	break;
    case K_LINK:
	mov_const (R0, a + 1 + n);
	STR (R0, R4, REG (11));
	break;
    case K_SEX:
	load_reg (R0, s, a);
	dp_r (OP_MOV, R2, 0, R0, SH_LSL, 24);
	dp_r (OP_MOV, R2, 0, R2, SH_ASR, 24);
	store_sz (R2, in);
	set_dreg (R2, d);
	break;
    case K_LOB:
    case K_HIB:
	/* v = the low (lob) or high (hib) byte; vSign = vZero = v << 8 */
	load_reg (R0, s, a);
	if (in.kind == K_LOB)
	    dp_i (OP_AND, R2, R0, 0xff);
	else
	{
	    dp_r (OP_MOV, R2, 0, R0, SH_LSR, 8);
	    dp_i (OP_AND, R2, R2, 0xff);
	}
	if (in.store & (F_S | F_Z))
	{
	    dp_r (OP_MOV, R3, 0, R2, SH_LSL, 8);
	    store_sz (R3, in);
	}
	set_dreg (R2, d);
	break;
    case K_INC:
    case K_DEC:
	LDR (R0, R4, REG (n));
	dp_i (in.kind == K_INC ? OP_ADD : OP_SUB, R0, R0, 1);
	STR (R0, R4, REG (n));
	store_sz (R0, in);
	if (n == 14)
	    read_r14 ();
	break;
    case K_GETB:
	LDRB (R2, R4, G (vRomBuffer));
	set_dreg (R2, d);
	break;
    case K_GETBH:	/* USEX8 (SREG) | rb << 8 */
	load_reg (R0, s, a);
	LDRB (R1, R4, G (vRomBuffer));
	dp_i (OP_AND, R2, R0, 0xff);
	dp_r (OP_ORR, R2, R2, R1, SH_LSL, 8);
	set_dreg (R2, d);
	break;
    case K_GETBL:	/* (SREG & 0xff00) | rb */
	load_reg (R0, s, a);
	LDRB (R1, R4, G (vRomBuffer));
	dp_i (OP_AND, R2, R0, 0xff00);
	dp_r (OP_ORR, R2, R2, R1);
	set_dreg (R2, d);
	break;
    case K_GETBS:	/* SEX8 (rb) */
	LDRB (R1, R4, G (vRomBuffer));
	dp_r (OP_MOV, R2, 0, R1, SH_LSL, 24);
	dp_r (OP_MOV, R2, 0, R2, SH_ASR, 24);
	set_dreg (R2, d);
	break;
    case K_IBT:
	mov_const (R0, (uint32) (int32) (int8) in.imm);
	set_dreg (R0, n);
	break;
    case K_IWT:
	mov_const (R0, in.imm);
	set_dreg (R0, n);
	break;
    case K_LMS:
    case K_LM:
	/* lms: (yy) = imm << 1, the high byte at + 1; lm: (xx), at ^ 1 */
	{
	    uint32 ram_addr = in.kind == K_LMS ? in.imm << 1 : in.imm;
	    uint32 second = in.kind == K_LMS ? ram_addr + 1 : ram_addr ^ 1;
	    mov_const (R0, ram_addr);
	    STR (R0, R4, G (vLastRamAdr));
	    LDR (R2, R4, G (pvRamBank));
	    mov_const (R3, ram_addr & 0xffff);
	    mem_r (true, R1, R2, R3, SH_LSL, 0);
	    mov_const (R3, second & 0xffff);
	    mem_r (true, R3, R2, R3, SH_LSL, 0);
	    dp_r (OP_ORR, R1, R1, R3, SH_LSL, 8);
	    set_dreg (R1, n);
	}
	break;
    case K_SMS:
    case K_SM:
	{
	    uint32 ram_addr = in.kind == K_SMS ? in.imm << 1 : in.imm;
	    uint32 second = in.kind == K_SMS ? ram_addr + 1 : ram_addr ^ 1;
	    load_reg (R1, n, a);
	    mov_const (R0, ram_addr);
	    STR (R0, R4, G (vLastRamAdr));
	    LDR (R2, R4, G (pvRamBank));
	    mov_const (R3, ram_addr & 0xffff);
	    mem_r (false, R1, R2, R3, SH_LSL, 0);
	    dp_r (OP_MOV, R1, 0, R1, SH_LSR, 8);
	    mov_const (R3, second & 0xffff);
	    mem_r (false, R1, R2, R3, SH_LSL, 0);
	}
	break;
    default:
	break;
    }
}

/* ---- Blocks ---- */

/* An exit that the dispatcher may point at the block it leads to */
struct Exit
{
    uint32 *site;		/* the branch to the stub */
    uint32 next;		/* the next instruction's address (R15 - 1) */
    State st;
    uint32 insns;		/* (counted runs) */
};

#define MAX_INSNS  64		/* per block */
#define MAX_EXITS  4

static Exit exits [MAX_EXITS];
static int exit_count;
static bool counted;
static uint32 block_start;
static State block_state;
static uint32 *block_code;

/* Stores what the exit leaves: the prefix state (uses r0, r1), R15 =
 * next + 1 and the pipe holding the byte at next (static next), and in
 * counted runs the instructions run. */
static void store_exit_state (State st, uint32 insns)
{
    LDR (R0, R4, G (vStatusReg));
    dp_i (OP_BIC, R0, R0, FLG_ALT1 | FLG_ALT2 | FLG_B);
    uint32 bits = (st.alt & 1 ? FLG_ALT1 : 0) | (st.alt & 2 ? FLG_ALT2 : 0) | (st.b ? FLG_B : 0);
    if (bits)
	dp_i (OP_ORR, R0, R0, bits);
    STR (R0, R4, G (vStatusReg));
    store_regs_pointers (st);
    if (counted)
    {
	mov_const (R0, (uint32) (uintptr_t) &counted_insns);
	mov_const (R1, insns);
	STR (R1, R0, 0);
    }
}

/* Goes on at a known address in state st: straight into its block if it's
 * there (or this one), else through a stub to the dispatcher. */
static void exit_to (uint32 next, State st, uint32 insns)
{
    bool linkable = !counted && next <= 0xffff;
    if (linkable)
    {
	uint32 *target = next == block_start && same_state (st, block_state) ? block_code :
	    find_block (code_pbr, next, st);
	if (target)
	{
	    b_to (target);
	    return;
	}
    }
    if (exit_count < MAX_EXITS)
    {
	Exit &e = exits [exit_count++];
	e.site = out;
	e.next = next;
	e.st = st;
	e.insns = insns;
	put (0);		/* (the branch to the stub, set when the stub is placed) */
	return;
    }
    /* (no room for another stub: store it here) */
    store_exit_state (st, insns);
    mov_const (R0, next + 1);
    STR (R0, R4, REG (15));
    mov_const (R0, code_byte (next));
    STRB (R0, R4, G (vPipe));
    dp_i (OP_MOV, R0, 0, 0);
    b_to (exit_code);
}

/* The stubs: each stores its exit's state and returns its site to patch
 * (0 if it mustn't be patched). */
static void place_stubs (void)
{
    for (int i = 0; i < exit_count; i++)
    {
	Exit &e = exits [i];
	if (out >= out_limit)
	{
	    out_of_room = true;
	    return;
	}
	*e.site = branch_word (e.site, out, C_AL);
	store_exit_state (e.st, e.insns);
	mov_const (R0, e.next + 1);
	STR (R0, R4, REG (15));
	mov_const (R0, code_byte (e.next));
	STRB (R0, R4, G (vPipe));
	if (!counted && e.next <= 0xffff)
	    mov_const (R0, (uint32) (uintptr_t) e.site);
	else
	    dp_i (OP_MOV, R0, 0, 0);
	b_to (exit_code);
    }
}

/* Leaves for an address in r6 (jmp, loop's R13): R15 = r6 + 1, the pipe
 * = the program byte at r6. */
static void exit_dynamic (State st, uint32 insns)
{
    store_exit_state (st, insns);
    dp_i (OP_ADD, R0, R6, 1);
    STR (R0, R4, REG (15));
    LDR (R1, R4, G (pvPrgBank));
    dp_r (OP_MOV, R2, 0, R6, SH_LSL, 16);
    mem_r (true, R1, R1, R2, SH_LSR, 16);
    STRB (R1, R4, G (vPipe));
    dp_i (OP_MOV, R0, 0, 0);
    b_to (exit_code);
}

/* The condition of a branch (opcode 06-0f) into the ARM flags; returns the
 * ARM condition that takes it. */
static int branch_condition (uint8 op)
{
    switch (op)
    {
    case 0x08: case 0x09:	/* bne, beq: (uint16) vZero == 0 */
	LDR (R0, R4, G (vZero));
	dp_r (OP_MOV, R0, 0, R0, SH_LSL, 16, C_AL, 1);
	return op == 0x08 ? C_NE : C_EQ;
    case 0x0a: case 0x0b:	/* bpl, bmi: vSign & 0x8000 */
	LDR (R0, R4, G (vSign));
	dp_i (OP_TST, 0, R0, 0x8000, C_AL, 1);
	return op == 0x0a ? C_EQ : C_NE;
    case 0x0c: case 0x0d:	/* bcc, bcs: vCarry & 1 */
	LDR (R0, R4, G (vCarry));
	dp_i (OP_TST, 0, R0, 1, C_AL, 1);
	return op == 0x0c ? C_EQ : C_NE;
    case 0x0e: case 0x0f:	/* bvc, bvs: (uint32) (vOverflow + 0x8000) >= 0x10000 */
	LDR (R0, R4, G (vOverflow));
	dp_i (OP_ADD, R0, R0, 0x8000);
	dp_i (OP_CMP, 0, R0, 0x10000, C_AL, 1);
	return op == 0x0e ? C_CC : C_CS;
    default:			/* 06 bge, 07 blt: (S != 0) == (OV != 0) */
	LDR (R0, R4, G (vOverflow));
	dp_i (OP_ADD, R0, R0, 0x8000);
	dp_i (OP_CMP, 0, R0, 0x10000, C_AL, 1);
	dp_i (OP_MOV, R1, 0, 0, C_CC);
	dp_i (OP_MOV, R1, 0, 1, C_CS);
	LDR (R2, R4, G (vSign));
	dp_i (OP_AND, R2, R2, 0x8000);
	dp_r (OP_CMP, 0, R1, R2, SH_LSR, 15, C_AL, 1);
	return op == 0x06 ? C_EQ : C_NE;
    }
}

/* Translates a block at addr (in the program bank pbr) entered in state
 * st, of at most max instructions; NULL if its first instruction isn't
 * translated. */
static uint32 *translate (uint32 pbr, uint32 addr, State entry, uint32 max)
{
    Insn list [MAX_INSNS + 1];
    uint8 prefix [MAX_INSNS + 1];	/* list [i] is a prefix (no code) */
    int count = 0;
    State st = entry;
    uint32 at = addr;
    int end = 0;			/* 0: falls through to 'at' in state st */
    if (max > MAX_INSNS)
	max = MAX_INSNS;

    code_pbr = pbr;
    prg = GSU.apvRomBank [pbr];

    while ((uint32) count < max)
    {
	Insn in;
	State before = st;
	bool insn = decode (at, st, in);
	if (insn && in.kind == K_UNSUPPORTED)
	    break;
	if (insn && control (in.kind))
	{
	    /* It and its delay slot, or stop before it */
	    if ((uint32) count + 2 > max)
		break;
	    State after = in.kind == K_BRA || in.kind == K_BCOND ? before : State { 0, 0, 0, 0 };
	    Insn slot;
	    State slot_state = after;
	    bool slot_insn = decode (at + in.len, slot_state, slot);
	    if (!good_delay_slot (slot, !slot_insn))
		break;
	    prefix [count] = 0;
	    list [count++] = in;
	    prefix [count] = !slot_insn;
	    if (slot_insn)
		slot.st = after;
	    list [count++] = slot;
	    st = slot_insn ? State { 0, 0, 0, 0 } : slot_state;
	    end = in.kind;
	    break;
	}
	prefix [count] = !insn;
	list [count++] = in;
	at += insn ? in.len : 1;
	if (insn)
	{
	    if (in.kind == K_STOP)
	    {
		end = K_STOP;
		break;
	    }
	    st = State { 0, 0, 0, 0 };
	}
	if (at > 0xffff)
	    break;
    }
    if (count == 0)
	return NULL;

    /* Which flags each instruction must store: those something reads
     * before they're set again (all of them at the end) */
    uint8 live = F_ALL;
    for (int i = count - 1; i >= 0; i--)
	if (!prefix [i])
	{
	    list [i].store = list [i].writes & live;
	    live = (live & ~list [i].writes) | (list [i].reads & F_ALL);
	}

    out = code_end;
    out_limit = fx_jit_code + CODE_WORDS - 64;
    bad_code = false;
    block_code = out;
    block_start = addr;
    block_state = entry;
    exit_count = 0;

    int body = end && end != K_STOP ? count - 2 : count;	/* before the control instruction */
    uint32 insns = 0;
    for (int i = 0; i < body; i++)
    {
	insns++;
	if (prefix [i])
	    continue;
	if (list [i].kind == K_STOP)
	{
	    /* G off, IRQ unless CFGR bit 7, POR 0, a nop in the pipe, R15 + 1 */
	    LDR (R0, R4, G (vStatusReg));
	    dp_i (OP_BIC, R0, R0, FLG_G | FLG_ALT1 | FLG_ALT2 | FLG_B);
	    LDR (R1, R4, G (pvRegisters));
	    LDRB (R1, R1, GSU_CFGR);
	    dp_i (OP_TST, 0, R1, 0x80, C_AL, 1);
	    dp_i (OP_ORR, R0, R0, FLG_IRQ, C_EQ);
	    STR (R0, R4, G (vStatusReg));
	    dp_i (OP_MOV, R0, 0, 0);
	    STR (R0, R4, G (vPlotOptionReg));
	    dp_i (OP_MOV, R0, 0, 1);
	    STRB (R0, R4, G (vPipe));
	    store_regs_pointers (State { 0, 0, 0, 0 });
	    mov_const (R0, list [i].addr + 2);
	    STR (R0, R4, REG (15));
	    if (counted)
	    {
		mov_const (R0, (uint32) (uintptr_t) &counted_insns);
		mov_const (R1, insns);
		STR (R1, R0, 0);
	    }
	    dp_i (OP_MOV, R0, 0, 0);
	    b_to (exit_code);
	    break;
	}
	emit_insn (list [i]);
    }

    if (end && end != K_STOP)
    {
	const Insn &c = list [count - 2];
	const Insn &slot = list [count - 1];
	bool slot_code = !prefix [count - 1];
	uint32 total = insns + 2;
	uint32 a = c.addr;
	switch (end)
	{
	case K_BRA:
	case K_IWT_R15:
	    {
		uint32 target = end == K_BRA ? a + 2 + (uint32) (int32) (int8) code_byte (a + 1) : c.imm;
		if (slot_code)
		    emit_insn (slot);
		exit_to (target, st, total);
	    }
	    break;
	case K_BCOND:
	    {
		uint32 target = a + 2 + (uint32) (int32) (int8) code_byte (a + 1);
		int cond = branch_condition (c.op);
		uint32 *taken = out;
		put (0);			/* b<cond> to the taken path */
		if (slot_code)
		    emit_insn (slot);
		exit_to (a + 3, st, total);
		*taken = branch_word (taken, out, cond);
		if (slot_code)
		    emit_insn (slot);
		exit_to (target, st, total);
	    }
	    break;
	case K_LOOP:
	    {
		/* R12 - 1; vSign = vZero = it; to R13 unless (uint16) R12 is 0 */
		LDR (R0, R4, REG (12));
		dp_i (OP_SUB, R0, R0, 1);
		STR (R0, R4, REG (12));
		store_sz (R0, c);
		dp_r (OP_MOV, R0, 0, R0, SH_LSL, 16, C_AL, 1);
		uint32 *done = out;
		put (0);			/* beq to the end of the loop */
		LDR (R6, R4, REG (13));
		if (slot_code)
		    emit_insn (slot);
		/* R13 as it was when this was translated: straight there */
		uint32 r13 = GSU.avReg [13];
		if (!counted && r13 <= 0xffff)
		{
		    mov_const (R0, r13);
		    dp_r (OP_CMP, 0, R6, R0, SH_LSL, 0, C_AL, 1);
		    uint32 *other = out;
		    put (0);			/* bne to the dynamic exit */
		    exit_to (r13, st, total);
		    *other = branch_word (other, out, C_NE);
		}
		exit_dynamic (st, total);
		*done = branch_word (done, out, C_EQ);
		if (slot_code)
		    emit_insn (slot);
		exit_to (a + 2, st, total);
	    }
	    break;
	case K_JMP:
	    LDR (R6, R4, REG (c.op & 15));
	    if (slot_code)
		emit_insn (slot);
	    exit_dynamic (st, total);
	    break;
	}
    }
    else if (end != K_STOP)
	exit_to (at, st, insns);

    place_stubs ();
    if (out_of_room || bad_code)
	return NULL;
    code_end = out;
    return block_code;
}

/* ---- Running ---- */

/* The code at the start of the buffer: enter (code, &GSU, table) and exit */
static void setup (void)
{
    out = fx_jit_code;
    out_limit = fx_jit_code + CODE_WORDS;
    enter_code = out;
    put (0xE92D5FF0);			/* push {r4-r12, lr} */
    dp_r (OP_MOV, R4, 0, R1);
    dp_r (OP_MOV, R5, 0, R2);
    put (0xE12FFF10 | R0);		/* bx r0 */
    exit_code = out;
    put (0xE8BD9FF0);			/* pop {r4-r12, pc} */
    code_end = out;
    memset (blocks, 0, sizeof blocks);
    block_count = 0;
    flushes++;
    fx_jit_stats [9]++;
    out_of_room = false;
}

/* The new code (or a patched branch) from the data cache to memory, and the
 * instruction cache's copy dropped. Not in the qemu tests (user mode). */
static void sync_code (const uint32 *from, const uint32 *to)
{
#ifdef FX_JIT_CACHE_OPS
    uintptr_t a = (uintptr_t) from & ~31u;
    for (; a < (uintptr_t) to; a += 32)
	__asm__ volatile ("mcr p15, 0, %0, c7, c10, 1" : : "r" (a));	/* clean D */
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" : : "r" (0));		/* drain write buffer */
    for (a = (uintptr_t) from & ~31u; a < (uintptr_t) to; a += 32)
	__asm__ volatile ("mcr p15, 0, %0, c7, c5, 1" : : "r" (a));	/* invalidate I */
#else
    (void) from;
    (void) to;
#endif
}

static bool ready;
static int (*entry) (uint32 *, struct FxRegs_s *, void (**) ());

/* Under qemu-arm (the tests) the buffer is in a segment that isn't
 * executable: Linux's mprotect, which qemu-arm's user mode runs. */
static void make_executable (void)
{
#ifdef FX_JIT_MPROTECT
    register uint32 r0 __asm__ ("r0") = (uint32) (uintptr_t) fx_jit_code & ~0xfffu;
    register uint32 r1 __asm__ ("r1") = sizeof (fx_jit_code) + 0x1000;
    register uint32 r2 __asm__ ("r2") = 7;	/* read, write, execute */
    register uint32 r7 __asm__ ("r7") = 125;	/* mprotect */
    __asm__ volatile ("svc #0" : "+r" (r0) : "r" (r1), "r" (r2), "r" (r7) : "memory");
#endif
}

void fx_jit_flush (void)
{
    if (!ready)
    {
	make_executable ();
#ifdef FX_JIT_STATS_PRINT
	atexit (print_stats);
#endif
    }
    setup ();
    sync_code (fx_jit_code, code_end);
    entry = (int (*) (uint32 *, struct FxRegs_s *, void (**) ())) enter_code;
    ready = true;
}

/* The state the next instruction runs in, if it can run translated: its
 * address R15 - 1 in the bank (no bits above 16), the pipe holding its
 * byte, code in ROM. */
static bool translatable (uint32 &addr, State &st)
{
    uint32 r15 = GSU.avReg [15];
    addr = r15 - 1;
    if (addr > 0xffff || GSU.vPrgBankReg >= 0x70 || GSU.vPipe != GSU.pvPrgBank [addr])
	return false;
    uint32 sfr = GSU.vStatusReg;
    st.alt = (sfr & FLG_ALT1 ? 1 : 0) | (sfr & FLG_ALT2 ? 2 : 0);
    st.b = sfr & FLG_B ? 1 : 0;
    st.s = (uint8) (GSU.pvSreg - GSU.avReg);
    st.d = (uint8) (GSU.pvDreg - GSU.avReg);
    return true;
}

static uint32 *block_for (uint32 addr, State st)
{
    uint32 pbr = GSU.vPrgBankReg;
    uint32 *code = find_block (pbr, addr, st);
    if (code)
	return code;
    uint32 *start = code_end;
    code = translate (pbr, addr, st, MAX_INSNS);
    fx_jit_stats [6]++;
    if (!code)
    {
	fx_jit_stats [7]++;
#ifdef FX_JIT_STATS_PRINT
	log_key (fail_key, fail_count, pbr << 27 | addr << 11 | state_bits (st));
#endif
    }
    if (!code && out_of_room)
    {
	fx_jit_flush ();
	start = code_end;
	code = translate (pbr, addr, st, MAX_INSNS);
    }
    if (!code)
	return NULL;
    sync_code (start, code_end);
    if (!add_block (pbr, addr, st, code))
    {
	/* The table is full: start again */
	fx_jit_flush ();
	start = code_end;
	code = translate (pbr, addr, st, MAX_INSNS);
	if (!code)
	    return NULL;
	sync_code (start, code_end);
	add_block (pbr, addr, st, code);
    }
    return code;
}

void fx_jit_run (void)
{
    if (!ready)
	fx_jit_flush ();
    /* A run starts by reading the ROM buffer, like the interpreters */
    GSU.vRomBuffer = GSU.pvRomBank [GSU.avReg [14] & 0xffff];
    while (GSU.vStatusReg & FLG_G)
    {
	uint32 addr;
	State st;
	if (!translatable (addr, st))
	{
	    if (GSU.vPrgBankReg >= 0x70)
	    {
		fx_step (~0u);		/* code in RAM: the interpreter to the end */
		fx_jit_stats [10]++;
	    }
	    else
	    {
		fx_step (1);
		fx_jit_stats [11]++;
	    }
	    fx_jit_stats [4]++;
	    continue;
	}
	uint32 *code = block_for (addr, st);
	if (!code)
	{
	    fx_step (1);
	    fx_jit_stats [4]++;
	    continue;
	}
	fx_jit_stats [3]++;
	uint32 *site = (uint32 *) (uintptr_t) entry (code, &GSU, fx_apfOpcodeTable);
	if (!site)
	{
	    fx_jit_stats [8]++;
#ifdef FX_JIT_STATS_PRINT
	    uint32 a2;
	    State s2;
	    if ((GSU.vStatusReg & FLG_G) && translatable (a2, s2))
		log_key (dyn_key, dyn_count, GSU.vPrgBankReg << 27 | a2 << 11 | state_bits (s2));
#endif
	}
	/* An exit to a block not translated yet: point it there */
	if (site && (GSU.vStatusReg & FLG_G) && translatable (addr, st))
	{
	    uint32 before = flushes;
	    uint32 *target = block_for (addr, st);
	    if (target && flushes == before)
	    {
		*site = branch_word (site, target, C_AL);
		sync_code (site, site + 1);
		fx_jit_stats [5]++;
	    }
	}
    }
}

/* For the tests: n instructions, blocks translated afresh to stop exactly
 * there (no linking). */
void fx_jit_run_counted (uint32 n)
{
    if (!ready)
	fx_jit_flush ();
    GSU.vRomBuffer = GSU.pvRomBank [GSU.avReg [14] & 0xffff];
    counted = true;
    while (n && (GSU.vStatusReg & FLG_G))
    {
	uint32 addr;
	State st;
	uint32 *code = NULL;
	if (translatable (addr, st))
	{
	    uint32 *start = code_end;
	    code = translate (GSU.vPrgBankReg, addr, st, n);
	    if (!code && out_of_room)
	    {
		fx_jit_flush ();
		start = code_end;
		code = translate (GSU.vPrgBankReg, addr, st, n);
	    }
	    if (code)
		sync_code (start, code_end);
	}
	if (!code)
	{
	    fx_step (1);
	    fx_jit_stats [1]++;
	    n--;
	    continue;
	}
	counted_insns = 0;
	fx_jit_stats [2]++;
	entry (code, &GSU, fx_apfOpcodeTable);
	fx_jit_stats [0] += counted_insns;
	n -= counted_insns;
	code_end = code;	/* (not kept) */
    }
    counted = false;
}

#endif
