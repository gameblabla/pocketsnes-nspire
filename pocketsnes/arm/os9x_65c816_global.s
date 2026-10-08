@ Memory access for the assembler CPU core: plain ROM and RAM directly, the
@ rest through the C core's own functions (os9x_asm_cpu.cpp), which add
@ their cycles themselves. psnes 7.2.0 handled the special areas here too,
@ for the memory map of Snes9x 1.39; this core's map has more kinds of area
@ (and the C core's ways of reading them), so they are left to C.
@
@ asmS9xGetByte/GetWord/SetByte/SetWord are reached with their return
@ address pushed (CALLMEM in os9x_65c816_common.s) and return
@ with LDMFD R13!,{PC}. asmS9xSetPCBase returns to r3.
@ The block number is (Address >> 12) & 0xFFF and the offset Address &
@ 0xFFFF, as in getset.h, so stray bits above the bank don't matter.

	.text
	.align	4
	.include	"os9x_65c816_common.s"

.globl asmS9xGetByte
.globl asmS9xGetWord
.globl asmS9xSetByte
.globl asmS9xSetWord
.globl asmS9xSetPCBase
.globl asmS9xGetStackWord
.globl asmS9xSetStackWord

@ input: r0 : address
@ return: rpc, regpcbase, MemSpeed (CPU.MemSpeed and rstatus)
asmS9xSetPCBase:
	mov	r1, r0, lsr #MEMMAP_SHIFT
	ldr	r2, [reg_cpu_var, #Map_ofs]
	bic	r1, r1, #0xFF000		@ block
	ldr	regpcbase, [r2, r1, lsl #2]	@ Map[block]
	cmp	regpcbase, #MAP_LAST
	blo	SPCBSpecial

	ldr	r2, [reg_cpu_var, #MemorySpeed_ofs]
	mov	r0, r0, lsl #16
	ldrb	r1, [r2, r1]			@ MemorySpeed[block]
	add	rpc, regpcbase, r0, lsr #16
	str	r1, [reg_cpu_var, #MemSpeed_ofs]
	bic	rstatus, rstatus, #MEMSPEED_MASK
	orr	rstatus, rstatus, r1, lsl #MEMSPEED_SHIFT
	bx	r3

SPCBSpecial:
	@ S9xSetPCBase sets CPU.PC, CPU.PCBase and CPU.MemSpeed
	stmfd	sp!, {r3}
	CCALL	asm_S9xSetPCBase
	ldmfd	sp!, {r3}
	ldr	regpcbase, [reg_cpu_var, #PCBase_ofs]
	ldr	rpc, [reg_cpu_var, #PC_ofs]
	ldr	r1, [reg_cpu_var, #MemSpeed_ofs]
	bic	rstatus, rstatus, #MEMSPEED_MASK
	orr	rstatus, rstatus, r1, lsl #MEMSPEED_SHIFT
	bx	r3


@ uint8 asmS9xGetByte(uint32 address);
asmS9xGetByte:
	@  in : R0  = 0x00hhmmll
	@  out : R0 = 0x000000ll
	@  DESTROYED : R1,R2,R3
	@  UPDATE : reg_cycles
	mov	r1, r0, lsr #MEMMAP_SHIFT
	ldr	r2, [reg_cpu_var, #Map_ofs]
	bic	r1, r1, #0xFF000		@ block
	ldr	r2, [r2, r1, lsl #2]		@ GetAddress = Map[block]
	cmp	r2, #MAP_LAST
	blo	GBSpecial

	ldr	r3, [reg_cpu_var, #MemorySpeed_ofs]
	mov	r0, r0, lsl #16
	ldrb	r0, [r2, r0, lsr #16]		@ *(GetAddress + (Address & 0xffff))
	ldrb	r3, [r3, r1]
	add	reg_cycles, reg_cycles, r3	@ CPU.Cycles += MemorySpeed[block]
	@  if BlockIsRAM => CPU.WaitAddress = CPU.PCAtOpcodeStart (idle loop detection)
	ldr	r3, [reg_cpu_var, #BlockIsRAM_ofs]
	ldrb	r3, [r3, r1]
	cmp	r3, #0
	ldrne	r1, [reg_cpu_var, #PCAtOpcodeStart_ofs]
	orrne	rstatus, rstatus, #MASK_SHUTDOWN
	strne	r1, [reg_cpu_var, #WaitAddress_ofs]
	ldmfd	sp!, {pc}

GBSpecial:
	SYNC_TO_C
	CCALL	asm_S9xGetByte
	SYNC_FROM_C
	ldmfd	sp!, {pc}


@ uint16 asmS9xGetWord(uint32 address);
asmS9xGetWord:
	@  in : R0  = 0x00hhmmll
	@  out : R0 = 0x0000hhll
	@  DESTROYED : R1,R2,R3
	@  UPDATE : reg_cycles
	mov	r1, r0, lsl #20
	cmn	r1, #0x00100000			@ (Address & 0xfff) == 0xfff: two blocks,
	beq	GWSpecial			@ getset.h reads it byte by byte
	mov	r1, r0, lsr #MEMMAP_SHIFT
	ldr	r2, [reg_cpu_var, #Map_ofs]
	bic	r1, r1, #0xFF000		@ block
	ldr	r2, [r2, r1, lsl #2]		@ GetAddress = Map[block]
	cmp	r2, #MAP_LAST
	blo	GWSpecial

	ldr	r3, [reg_cpu_var, #MemorySpeed_ofs]
	mov	r0, r0, lsl #16
	add	r2, r2, r0, lsr #16		@ GetAddress + (Address & 0xffff)
	ldrb	r3, [r3, r1]
	add	reg_cycles, reg_cycles, r3, lsl #1	@ CPU.Cycles += MemorySpeed[block] << 1
	tst	r2, #1
	ldreqh	r0, [r2]
	ldrneb	r0, [r2]
	ldrneb	r3, [r2, #1]
	orrne	r0, r0, r3, lsl #8
	@  if BlockIsRAM => CPU.WaitAddress = CPU.PCAtOpcodeStart
	ldr	r3, [reg_cpu_var, #BlockIsRAM_ofs]
	ldrb	r3, [r3, r1]
	cmp	r3, #0
	ldrne	r1, [reg_cpu_var, #PCAtOpcodeStart_ofs]
	orrne	rstatus, rstatus, #MASK_SHUTDOWN
	strne	r1, [reg_cpu_var, #WaitAddress_ofs]
	ldmfd	sp!, {pc}

GWSpecial:
	SYNC_TO_C
	CCALL	asm_S9xGetWord
	SYNC_FROM_C
	ldmfd	sp!, {pc}


@ void asmS9xSetByte(uint32 address,uint8 val);
asmS9xSetByte:
	@  in : R0=0x00hhmmll  R1=0x000000ll
	@  DESTROYED : R0,R1,R2,R3
	@  UPDATE : reg_cycles
	bic	rstatus, rstatus, #MASK_SHUTDOWN	@ CPU.WaitAddress = NULL
	mov	r3, r0, lsr #MEMMAP_SHIFT
	ldr	r2, [reg_cpu_var, #WriteMap_ofs]
	bic	r3, r3, #0xFF000		@ block
	ldr	r2, [r2, r3, lsl #2]		@ SetAddress = WriteMap[block]
	cmp	r2, #MAP_LAST
	blo	SBSpecial

	mov	r0, r0, lsl #16
	strb	r1, [r2, r0, lsr #16]		@ *(SetAddress + (Address & 0xffff)) = Byte
	ldr	r0, [reg_cpu_var, #MemorySpeed_ofs]
	ldrb	r0, [r0, r3]
	add	reg_cycles, reg_cycles, r0	@ CPU.Cycles += MemorySpeed[block]
	ldmfd	sp!, {pc}

SBSpecial:
	SYNC_TO_C
	CCALL	asm_S9xSetByte
	SYNC_FROM_C
	ldmfd	sp!, {pc}


@ void asmS9xSetWord(uint32 address,uint16 val);
asmS9xSetWord:
	@  in : R0  = 0x00hhmmll R1=0x0000hhll
	@  DESTROYED : R0,R1,R2,R3
	@  UPDATE : reg_cycles
	bic	rstatus, rstatus, #MASK_SHUTDOWN	@ CPU.WaitAddress = NULL
	mov	r3, r0, lsl #20
	cmn	r3, #0x00100000			@ (Address & 0xfff) == 0xfff: two blocks,
	beq	SWSpecial			@ getset.h writes it byte by byte
	mov	r3, r0, lsr #MEMMAP_SHIFT
	ldr	r2, [reg_cpu_var, #WriteMap_ofs]
	bic	r3, r3, #0xFF000		@ block
	ldr	r2, [r2, r3, lsl #2]		@ SetAddress = WriteMap[block]
	cmp	r2, #MAP_LAST
	blo	SWSpecial

	mov	r0, r0, lsl #16
	add	r2, r2, r0, lsr #16		@ SetAddress + (Address & 0xffff)
	strb	r1, [r2]
	mov	r1, r1, lsr #8
	strb	r1, [r2, #1]
	ldr	r0, [reg_cpu_var, #MemorySpeed_ofs]
	ldrb	r0, [r0, r3]
	add	reg_cycles, reg_cycles, r0, lsl #1	@ CPU.Cycles += MemorySpeed[block] << 1
	ldmfd	sp!, {pc}

SWSpecial:
	SYNC_TO_C
	CCALL	asm_S9xSetWord
	SYNC_FROM_C
	ldmfd	sp!, {pc}


@ Stack words, as cpuops.cpp's PullW and PushW move them: two bytes in bank 0,
@ the second wrapping from $FFFF to $0000. Plain RAM is done here; at the
@ end of a block, or anywhere special, the C core moves the two bytes.
@ uint16 asmS9xGetStackWord(uint32 address);	@ address = S + 1
asmS9xGetStackWord:
	@  in : R0  = S + 1 (up to 0x10000)
	@  out : R0 = 0x0000hhll
	@  DESTROYED : R1,R2,R3
	@  UPDATE : reg_cycles
	mov	r0, r0, lsl #16
	mov	r0, r0, lsr #16			@ bank 0, $FFFF + 1 = $0000
	mov	r1, r0, lsl #20
	cmn	r1, #0x00100000			@ (Address & 0xfff) == 0xfff: two blocks
	beq	GSWSpecial
	mov	r1, r0, lsr #MEMMAP_SHIFT	@ block (bank 0)
	ldr	r2, [reg_cpu_var, #Map_ofs]
	ldr	r2, [r2, r1, lsl #2]
	cmp	r2, #MAP_LAST
	blo	GSWSpecial

	ldr	r3, [reg_cpu_var, #MemorySpeed_ofs]
	add	r2, r2, r0
	ldrb	r3, [r3, r1]
	add	reg_cycles, reg_cycles, r3, lsl #1
	tst	r2, #1
	ldreqh	r0, [r2]
	ldrneb	r0, [r2]
	ldrneb	r3, [r2, #1]
	orrne	r0, r0, r3, lsl #8
	ldr	r3, [reg_cpu_var, #BlockIsRAM_ofs]
	ldrb	r3, [r3, r1]
	cmp	r3, #0
	ldrne	r1, [reg_cpu_var, #PCAtOpcodeStart_ofs]
	orrne	rstatus, rstatus, #MASK_SHUTDOWN
	strne	r1, [reg_cpu_var, #WaitAddress_ofs]
	ldmfd	sp!, {pc}

GSWSpecial:
	SYNC_TO_C
	CCALL	asm_S9xGetStackWord
	SYNC_FROM_C
	ldmfd	sp!, {pc}

@ void asmS9xSetStackWord(uint32 address, uint16 val);	@ address = S - 1
asmS9xSetStackWord:
	@  in : R0  = S - 1 (down to -1)  R1=0x0000hhll
	@  DESTROYED : R0,R1,R2,R3
	@  UPDATE : reg_cycles
	bic	rstatus, rstatus, #MASK_SHUTDOWN	@ CPU.WaitAddress = NULL
	mov	r0, r0, lsl #16
	mov	r0, r0, lsr #16			@ bank 0, $0000 - 1 = $FFFF
	mov	r3, r0, lsl #20
	cmn	r3, #0x00100000			@ (Address & 0xfff) == 0xfff: two blocks
	beq	SSWSpecial
	mov	r3, r0, lsr #MEMMAP_SHIFT	@ block (bank 0)
	ldr	r2, [reg_cpu_var, #WriteMap_ofs]
	ldr	r2, [r2, r3, lsl #2]
	cmp	r2, #MAP_LAST
	blo	SSWSpecial

	add	r2, r2, r0
	strb	r1, [r2]
	mov	r1, r1, lsr #8
	strb	r1, [r2, #1]
	ldr	r0, [reg_cpu_var, #MemorySpeed_ofs]
	ldrb	r0, [r0, r3]
	add	reg_cycles, reg_cycles, r0, lsl #1
	ldmfd	sp!, {pc}

SSWSpecial:
	SYNC_TO_C
	CCALL	asm_S9xSetStackWord
	SYNC_FROM_C
	ldmfd	sp!, {pc}

	.pool
