@ The 65c816 CPU core in ARM assembler, from psnes 7.2.0 (DrPocketSnes for
@ the GP2X, Wiz and Caanoo): Yoyofr's core from OpenSnes9x, with changes by
@ Squidge, notaz and Bitrider. Changed for PocketSNES on the TI-Nspire:
@ - CPU (struct SCPUState) offsets are those of our snes9x.h; os9x_asm_cpu.cpp
@   checks them, and copies the registers in and out (CPU.AsmRegisters).
@ - Anything other than plain ROM and RAM goes through the C core's own
@   S9xGetByte etc. (os9x_65c816_global.s), so both cores share one memory map.
@ - No sound CPU to run (the asmAPU_EXECUTE macros are gone).
@ - Interrupts, idle loop detection and cycle counts follow the C core.
@ - Calls into C keep the stack 8-byte aligned, as the EABI wants.

/****************************************************************
	DEFINES
****************************************************************/

@ CMemory::MAP_LAST: map entries below it are special (os9x_asm_cpu.cpp checks)
.equ MAP_LAST,	18

rstatus 	.req R4  @ format : 0xff800000
reg_d_bank	.req R4  @ format : 0x000000ll
reg_a		.req R5  @ format : 0xhhll0000 or 0xll000000
reg_d		.req R6  @ format : 0xhhll0000
reg_p_bank	.req R6  @ format : 0x000000ll
reg_x		.req R7  @ format : 0xhhll0000 or 0xll000000
reg_s		.req R8  @ format : 0x0000hhll
reg_y		.req R9  @ format : 0xhhll0000 or 0xll000000

rpc	    	.req R10 @ 32bits address
reg_cycles	.req R11 @ 32bits counter
regpcbase	.req R12 @ 32bits address

rscratch	.req R0  @ format : 0xhhll0000 if data and calculation or return of S9XREADBYTE	or WORD
regopcode	.req R0  @ format : 0x000000ll
rscratch2	.req R1  @ format : 0xhhll for calculation and value
rscratch3	.req R2  @ 
rscratch4	.req R3  @ ??????

@ used for SBC opcode
rscratch9	.req R10 @ ??????

reg_cpu_var .req R14



@ not used
@ R13	@ Pointer 32 bit on a struct.

@ R15 = pc (sic!)


/*
.equ Carry       1
.equ Zero        2
.equ IRQ         4
.equ Decimal     8
.equ IndexFlag  16
.equ MemoryFlag 32
.equ Overflow   64
.equ Negative  128
.equ Emulation 256*/

.equ STATUS_SHIFTER,		24
.equ MASK_APU_EXECUTING,(1<<(STATUS_SHIFTER-4))
.equ MASK_SHUTDOWN,	(1<<(STATUS_SHIFTER-3))
.equ MASK_BRANCHSKIP,	(1<<(STATUS_SHIFTER-2))
.equ MASK_EMUL,		(1<<(STATUS_SHIFTER-1))
.equ MASK_SHIFTER_CARRY,	(STATUS_SHIFTER+1)
.equ	MASK_CARRY,		(1<<(STATUS_SHIFTER))  @ 0
.equ	MASK_ZERO,		(2<<(STATUS_SHIFTER))  @ 1
.equ MASK_IRQ,		(4<<(STATUS_SHIFTER))  @ 2
.equ MASK_DECIMAL,		(8<<(STATUS_SHIFTER))  @ 3
.equ	MASK_INDEX,		(16<<(STATUS_SHIFTER)) @ 4  @ 1
.equ	MASK_MEM,		(32<<(STATUS_SHIFTER)) @ 5  @ 2
.equ	MASK_OVERFLOW,		(64<<(STATUS_SHIFTER)) @ 6  @ 4
.equ	MASK_NEG,		(128<<(STATUS_SHIFTER))@ 7  @ 8

.equ ONE_CYCLE, 6
.equ SLOW_ONE_CYCLE, 8
@ psnes added NOSA1 * n extra cycles to some branches and interrupts
@ (ADD1CYCLEX n etc.); the C core doesn't.
.equ NOSA1, 0

.equ	NMI_FLAG,	    (1 << 7)
.equ IRQ_PENDING_FLAG,    (1 << 11)
.equ SCAN_KEYS_FLAG,	    (1 << 4)


.equ MEMMAP_BLOCK_SIZE, (0x1000)
.equ MEMMAP_SHIFT, 12
.equ MEMMAP_MASK, (0xFFF)

.equ MEMSPEED_MASK, (0xF0000)
.equ MEMSPEED_SHIFT, 16

/****************************************************************
	MACROS
****************************************************************/

@ #include "os9x_65c816_mac_gen.h"
/*****************************************************************/
/*     Offsets in struct SCPUState (snes9x.h)                    */
/*****************************************************************/
.equ Flags_ofs,			0
.equ BranchSkip_ofs,		4
.equ NMIActive_ofs,		5
.equ IRQActive_ofs,		6
.equ WaitingForInterrupt_ofs,	7

@ CPU.AsmRegisters
.equ RPB_ofs,			8
.equ RDB_ofs,			9
.equ RP_ofs,			10
.equ RA_ofs,			12
.equ RAH_ofs,			13
.equ RD_ofs,			14
.equ RX_ofs,			16
.equ RS_ofs,			18
.equ RY_ofs,			20

.equ PC_ofs,			24
.equ Cycles_ofs,		28
.equ PCBase_ofs,		32
.equ PCAtOpcodeStart_ofs,	36
.equ WaitAddress_ofs,		40
.equ WaitCounter_ofs,		44
.equ NextEvent_ofs,		48
.equ V_Counter_ofs,		52
.equ MemSpeed_ofs,		56
.equ MemSpeedx2_ofs,		60
.equ FastROMSpeed_ofs,		64
.equ NMICycleCount_ofs,		76
.equ IRQCycleCount_ofs,		80
.equ InDMA_ofs,			84
.equ BRKTriggered_ofs,		87
.equ asm_OPTABLE_ofs,		88	@ CPU.AsmOpcodes
.equ OpenBus_ofs,		93	@ CPU.AsmOpenBus, which is OpenBus (globals.cpp)
.equ Map_ofs,			96	@ CPU.AsmMap = Memory.Map
.equ WriteMap_ofs,		100	@ CPU.AsmWriteMap = Memory.WriteMap
.equ MemorySpeed_ofs,		104	@ CPU.AsmMemorySpeed = Memory.MemorySpeed (bytes)
.equ BlockIsRAM_ofs,		108	@ CPU.AsmBlockIsRAM = Memory.BlockIsRAM
.equ HBlank_ofs,		112	@ CPU.AsmHBlank
.equ rstatus_ofs,		116	@ CPU.AsmStatus (tests: stray register bits, doStepOpcode)

/*****************************************************************/

@ Calls the C function \func with the arguments in r0 and r1, keeping the
@ stack 8-byte aligned (the EABI's rule for calls) at whatever depth the
@ caller is. r0 holds the result after; r1-r3 are lost; r12 (regpcbase) and
@ r14 (reg_cpu_var) are kept, r4-r11 are kept by the C function.
.macro		CCALL func
	mov	r3, sp
	bic	sp, sp, #7
	stmfd	sp!, {r2, r3, r12, r14}		@ r2 only pads to 16 bytes
	bl	\func
	ldmfd	sp!, {r2, r3, r12, r14}
	mov	sp, r3
.endm
@ The same through a function pointer in \reg (not r2 or r3).
.macro		CCALLREG reg
	mov	r3, sp
	bic	sp, sp, #7
	stmfd	sp!, {r2, r3, r12, r14}
	blx	\reg
	ldmfd	sp!, {r2, r3, r12, r14}
	mov	sp, r3
.endm

@ Around a call into the C core's memory functions (which may run a DMA or
@ answer a read of the sound ports): C sees the cycle count and, when the
@ idle loop detection is off, no wait address; after, the cycle count, the
@ wait address and a branch skip request come back. Uses r2 and r3, keeps r0.
.macro		SYNC_TO_C
	str	reg_cycles, [reg_cpu_var, #Cycles_ofs]
	tst	rstatus, #MASK_SHUTDOWN
	moveq	r3, #0
	streq	r3, [reg_cpu_var, #WaitAddress_ofs]
.endm
.macro		SYNC_FROM_C
	ldr	reg_cycles, [reg_cpu_var, #Cycles_ofs]
	ldr	r3, [reg_cpu_var, #WaitAddress_ofs]
	ldrb	r2, [reg_cpu_var, #BranchSkip_ofs]
	cmp	r3, #0
	biceq	rstatus, rstatus, #MASK_SHUTDOWN
	orrne	rstatus, rstatus, #MASK_SHUTDOWN
	cmp	r2, #0
	orrne	rstatus, rstatus, #MASK_BRANCHSKIP	@ the flag lives in rstatus while the core runs
	movne	r2, #0
	strneb	r2, [reg_cpu_var, #BranchSkip_ofs]
.endm


@ --------------

.macro		LOAD_REGS
    @ notaz
    add     r0,reg_cpu_var,#8
    ldmia   r0,{r1,reg_a,reg_x,reg_y,rpc,reg_cycles,regpcbase}
    @ rstatus (P) & reg_d_bank
    mov     reg_d_bank,r1,lsl #16
    mov     reg_d_bank,reg_d_bank,lsr #24
    mov     r0,r1,lsr #16
	orrs	rstatus, rstatus, r0,lsl #STATUS_SHIFTER @ 24
	@ if Carry set, then EMULATION bit was set
	orrcs	rstatus,rstatus,#MASK_EMUL	
    @ reg_d & reg_p_bank
    mov     reg_d,reg_a,lsr #16
    mov     reg_d,reg_d,lsl #8
    orr     reg_d,reg_d,r1,lsl #24
    mov     reg_d,reg_d,ror #24    @ 0xdddd00pb
    @ reg_x, reg_s
    mov     reg_s,reg_x,lsr #16
	@ Shift X,Y & A according to the current mode (INDEX, MEMORY bits)
	tst		rstatus,#MASK_INDEX
	movne	reg_x,reg_x,lsl #24
	movne	reg_y,reg_y,lsl #24
	moveq	reg_x,reg_x,lsl #16
	moveq	reg_y,reg_y,lsl #16
	tst		rstatus,#MASK_MEM
	movne	reg_a,reg_a,lsl #24
	moveq	reg_a,reg_a,lsl #16
	
	@-- load MemSpeed into rstatus
	ldr	r0, [reg_cpu_var, #MemSpeed_ofs]	
	bic	rstatus, rstatus, #MEMSPEED_MASK
	orr	rstatus, rstatus, r0, lsl #MEMSPEED_SHIFT
	
	@-- load BranchSkip into rstatus: while the core runs, the flag lives
	@-- there and CPU.BranchSkip stays clear (SAVE_REGS puts it back), so
	@-- that C setting it again can be seen (SYNC_FROM_C)
	ldrb	r0, [reg_cpu_var, #BranchSkip_ofs]
	movs	r0, r0
	biceq	rstatus, rstatus, #MASK_BRANCHSKIP
	orrne	rstatus, rstatus, #MASK_BRANCHSKIP
	movne	r0, #0
	strneb	r0, [reg_cpu_var, #BranchSkip_ofs]
	
	@-- load Shutdown
	ldr	r0, [reg_cpu_var, #WaitAddress_ofs]
	movs	r0, r0
	biceq	rstatus, rstatus, #MASK_SHUTDOWN
	orrne	rstatus, rstatus, #MASK_SHUTDOWN
		

/*
    @ reg_d & reg_p_bank share the same register
	LDRB		reg_p_bank,[reg_cpu_var,#RPB_ofs]
	LDRH		rscratch,[reg_cpu_var,#RD_ofs]
	ORR		reg_d,reg_d,rscratch, LSL #16	
	@ rstatus & reg_d_bank share the same register
	LDRB		reg_d_bank,[reg_cpu_var,#RDB_ofs]
	LDRH		rscratch,[reg_cpu_var,#RP_ofs]	
	ORRS		rstatus, rstatus, rscratch,LSL #STATUS_SHIFTER @ 24
	@ if Carry set, then EMULATION bit was set
	ORRCS		rstatus,rstatus,#MASK_EMUL	
	@ 
	LDRH		reg_a,[reg_cpu_var,#RA_ofs]		
	LDRH		reg_x,[reg_cpu_var,#RX_ofs]
	LDRH		reg_y,[reg_cpu_var,#RY_ofs]
	LDRH		reg_s,[reg_cpu_var,#RS_ofs]
	@ Shift X,Y & A according to the current mode (INDEX, MEMORY bits)
	TST		rstatus,#MASK_INDEX
	MOVNE		reg_x,reg_x,LSL #24
	MOVNE		reg_y,reg_y,LSL #24
	MOVEQ		reg_x,reg_x,LSL #16
	MOVEQ		reg_y,reg_y,LSL #16
	TST		rstatus,#MASK_MEM
	MOVNE		reg_a,reg_a,LSL #24
	MOVEQ		reg_a,reg_a,LSL #16
	
	LDR		regpcbase,[reg_cpu_var,#PCBase_ofs]
	LDR		rpc,[reg_cpu_var,#PC_ofs]	
	LDR		reg_cycles,[reg_cpu_var,#Cycles_ofs]
*/
.endm


.macro		SAVE_REGS
    @-- Shutdown flag: the wait address is only meaningful while it is set
	tst	rstatus, #MASK_SHUTDOWN
	moveq	r0, #0
	streq	r0, [reg_cpu_var, #WaitAddress_ofs]

    @-- Save BranchSkip flag
	tst	rstatus, #MASK_BRANCHSKIP
	moveq	r0, #0
	movne	r0, #1
	strb	r0, [reg_cpu_var, #BranchSkip_ofs]
	
    @-- Save MemSpeed2x for compatibility with other cores
	ldr	r0, [reg_cpu_var, #MemSpeed_ofs]
	mov	r0, r0, lsl #1
	str	r0, [reg_cpu_var, #MemSpeedx2_ofs]

    @ notaz
    @ reg_p_bank, reg_d_bank and rstatus
    mov 	r1, rstatus, lsr #16
    orr     r1, r1, reg_p_bank, lsl #24
	movs	r1, r1, lsr #8
	orrcs	r1, r1, #0x100 @ EMULATION bit
    orr     r1, r1, reg_d_bank, lsl #24
    mov     r1, r1, ror #16
    @ reg_a, reg_d
	tst		rstatus,#MASK_MEM
	ldrneh	r0, [reg_cpu_var,#RA_ofs]
	bicne	r0, r0,#0xFF
	orrne	reg_a, r0, reg_a,lsr #24	
	moveq	reg_a, reg_a, lsr #16
    mov     reg_d, reg_d, lsr #16
	orr  	reg_a, reg_a, reg_d, lsl #16
	@ Shift X&Y according to the current mode (INDEX, MEMORY bits)
	tst		rstatus,#MASK_INDEX
	movne	reg_x,reg_x,LSR #24
	movne	reg_y,reg_y,LSR #24
	moveq	reg_x,reg_x,LSR #16
	moveq	reg_y,reg_y,LSR #16
    @ reg_x, reg_s
	orr  	reg_x, reg_x, reg_s, lsl #16
    @ store
    add     r0,reg_cpu_var,#8
    stmia   r0,{r1,reg_a,reg_x,reg_y,rpc,reg_cycles,regpcbase}

/*
    @ reg_d & reg_p_bank is same register
	STRB		reg_p_bank,[reg_cpu_var,#RPB_ofs]
	MOV		rscratch,reg_d, LSR #16
	STRH		rscratch,[reg_cpu_var,#RD_ofs]
	@ rstatus & reg_d_bank is same register
	STRB		reg_d_bank,[reg_cpu_var,#RDB_ofs]
	MOVS		rscratch, rstatus, LSR #STATUS_SHIFTER  
	ORRCS		rscratch,rscratch,#0x100 @ EMULATION bit
	STRH		rscratch,[reg_cpu_var,#RP_ofs]
	@ 
	@ Shift X,Y & A according to the current mode (INDEX, MEMORY bits)
	TST		rstatus,#MASK_INDEX
	MOVNE		rscratch,reg_x,LSR #24
	MOVNE		rscratch2,reg_y,LSR #24
	MOVEQ		rscratch,reg_x,LSR #16
	MOVEQ		rscratch2,reg_y,LSR #16
	STRH		rscratch,[reg_cpu_var,#RX_ofs]
	STRH		rscratch2,[reg_cpu_var,#RY_ofs]
	TST		rstatus,#MASK_MEM
	LDRNEH		rscratch,[reg_cpu_var,#RA_ofs]
	BICNE		rscratch,rscratch,#0xFF
	ORRNE		rscratch,rscratch,reg_a,LSR #24	
	MOVEQ		rscratch,reg_a,LSR #16
	STRH		rscratch,[reg_cpu_var,#RA_ofs]
	
	STRH		reg_s,[reg_cpu_var,#RS_ofs]	
	STR		regpcbase,[reg_cpu_var,#PCBase_ofs]
	STR		rpc,[reg_cpu_var,#PC_ofs]
	
	STR		reg_cycles,[reg_cpu_var,#Cycles_ofs]
*/
.endm

/*****************************************************************/
.macro		ADD1CYCLE		
		add	reg_cycles,reg_cycles, #ONE_CYCLE		
.endm
.macro		ADD1CYCLENE
		addne	reg_cycles,reg_cycles, #ONE_CYCLE		
.endm		
.macro		ADD1CYCLEEQ
		addeq	reg_cycles,reg_cycles, #ONE_CYCLE		
.endm		

.macro		ADD2CYCLE
		add	reg_cycles,reg_cycles, #(ONE_CYCLE*2)
.endm
.macro		ADD2CYCLENE
		addne	reg_cycles,reg_cycles, #(ONE_CYCLE*2)
.endm
.macro		ADD2CYCLE2MEM		
		@ldr	rscratch,[reg_cpu_var,#MemSpeed_ofs]
		and	rscratch, rstatus, #MEMSPEED_MASK
		add	reg_cycles,reg_cycles, #(ONE_CYCLE*2)
		@add	reg_cycles, reg_cycles, rscratch, LSL #1
		add	reg_cycles, reg_cycles, rscratch, LSR #(MEMSPEED_SHIFT - 1)		
.endm
.macro		ADD2CYCLE1MEM
		@ldr	rscratch,[reg_cpu_var,#MemSpeed_ofs]
		and	rscratch, rstatus, #MEMSPEED_MASK
		add	reg_cycles,reg_cycles, #(ONE_CYCLE*2)
		@add	reg_cycles, reg_cycles, rscratch
		add	reg_cycles, reg_cycles, rscratch, LSR #MEMSPEED_SHIFT		
.endm

.macro		ADD3CYCLE
		add	reg_cycles,reg_cycles, #(ONE_CYCLE*3)
.endm

.macro		ADD1CYCLE1MEM
		@ldr	rscratch,[reg_cpu_var,#MemSpeed_ofs]
		and	rscratch, rstatus, #MEMSPEED_MASK
		add	reg_cycles,reg_cycles, #ONE_CYCLE
		@add	reg_cycles, reg_cycles, rscratch
		add	reg_cycles, reg_cycles, rscratch, LSR #MEMSPEED_SHIFT		
.endm

.macro		ADD1CYCLE2MEM
		@ldr	rscratch,[reg_cpu_var,#MemSpeed_ofs]
		and	rscratch, rstatus, #MEMSPEED_MASK
		add	reg_cycles,reg_cycles, #ONE_CYCLE
		@add	reg_cycles, reg_cycles, rscratch, LSL #1
		add	reg_cycles, reg_cycles, rscratch, LSR #(MEMSPEED_SHIFT - 1)		
.endm

.macro		ADD1MEM
		@ldr	rscratch,[reg_cpu_var,#MemSpeed_ofs]
		and	rscratch, rstatus, #MEMSPEED_MASK
		@add	reg_cycles, reg_cycles, rscratch
		add	reg_cycles, reg_cycles, rscratch, LSR #MEMSPEED_SHIFT		
.endm
			
.macro		ADD2MEM
		@ldr	rscratch,[reg_cpu_var,#MemSpeed_ofs]
		and	rscratch, rstatus, #MEMSPEED_MASK
		@add	reg_cycles, reg_cycles, rscratch, LSL #1
		add	reg_cycles, reg_cycles, rscratch, LSR #(MEMSPEED_SHIFT - 1)		
.endm
			
.macro		ADD3MEM
		@ldr	rscratch,[reg_cpu_var,#MemSpeed_ofs]
		and	rscratch, rstatus, #MEMSPEED_MASK
		@add	reg_cycles, reg_cycles, rscratch
		add	reg_cycles, reg_cycles, rscratch, LSR #MEMSPEED_SHIFT		
		@add	reg_cycles, reg_cycles, rscratch, LSL #1
		add	reg_cycles, reg_cycles, rscratch, LSR #(MEMSPEED_SHIFT - 1)		
.endm

/**************/
/*****************************************************************/
.macro		ADD1CYCLENEX x
		addne	reg_cycles,reg_cycles, #(ONE_CYCLE+(NOSA1*\x))		
.endm		
.macro		ADD1CYCLEEQX x
		addeq	reg_cycles,reg_cycles, #(ONE_CYCLE+(NOSA1*\x))		
.endm		
.macro		ADD2CYCLENEX x
		addne	reg_cycles,reg_cycles, #(ONE_CYCLE*2+(NOSA1*\x))
.endm

.macro		ADD1CYCLEX x		
		add	reg_cycles,reg_cycles, #(ONE_CYCLE+(NOSA1*\x))		
.endm
.macro		ADD2CYCLEX x
		add	reg_cycles,reg_cycles, #(ONE_CYCLE*2+(NOSA1*\x))
.endm
.macro		ADD2CYCLE2MEMX x		
		@ldr	rscratch,[reg_cpu_var,#MemSpeed_ofs]
		and	rscratch, rstatus, #MEMSPEED_MASK
		add	reg_cycles,reg_cycles, #(ONE_CYCLE*2+(NOSA1*\x))
		@add	reg_cycles, reg_cycles, rscratch, LSL #1
		add	reg_cycles, reg_cycles, rscratch, LSR #(MEMSPEED_SHIFT - 1)		
.endm
.macro		ADD2CYCLE1MEMX x
		@ldr	rscratch,[reg_cpu_var,#MemSpeed_ofs]
		and	rscratch, rstatus, #MEMSPEED_MASK
		add	reg_cycles,reg_cycles, #(ONE_CYCLE*2+(NOSA1*\x))
		@add	reg_cycles, reg_cycles, rscratch
		add	reg_cycles, reg_cycles, rscratch, LSR #MEMSPEED_SHIFT		
.endm

.macro		ADD3CYCLEX x
		add	reg_cycles,reg_cycles, #(ONE_CYCLE*3+(NOSA1*\x))
.endm

.macro		ADD1CYCLE1MEMX x
		@ldr	rscratch,[reg_cpu_var,#MemSpeed_ofs]
		and	rscratch, rstatus, #MEMSPEED_MASK
		add	reg_cycles,reg_cycles, #(ONE_CYCLE+(NOSA1*\x))
		@add	reg_cycles, reg_cycles, rscratch
		add	reg_cycles, reg_cycles, rscratch, LSR #MEMSPEED_SHIFT		
.endm

.macro		ADD1CYCLE2MEMX x
		@ldr	rscratch,[reg_cpu_var,#MemSpeed_ofs]
		and	rscratch, rstatus, #MEMSPEED_MASK
		add	reg_cycles,reg_cycles, #(ONE_CYCLE+(NOSA1*\x))
		@add	reg_cycles, reg_cycles, rscratch, LSL #1
		add	reg_cycles, reg_cycles, rscratch, LSR #(MEMSPEED_SHIFT - 1)		
.endm

/**************/

.macro		ClearDecimal
		BIC	rstatus,rstatus,#MASK_DECIMAL	
.endm			
.macro		SetDecimal
		ORR	rstatus,rstatus,#MASK_DECIMAL	
.endm
.macro		SetIRQ
		ORR	rstatus,rstatus,#MASK_IRQ
.endm						
.macro		ClearIRQ
		BIC	rstatus,rstatus,#MASK_IRQ
.endm

.macro		CPUShutdown
@ cpuops.cpp's CPUShutdown (with Settings.Shutdown on, as the frontend has it):
@ if (CPU.PC == CPU.WaitAddress)
		tst	rstatus, #MASK_SHUTDOWN
		beq	5431f
		LDR		rscratch,[reg_cpu_var,#WaitAddress_ofs]
		CMP		rpc,rscratch
		BNE		5431f
@ if (CPU.WaitCounter == 0 && !(CPU.Flags & (IRQ_PENDING_FLAG | NMI_FLAG)))
		LDR		rscratch,[reg_cpu_var,#Flags_ofs]
		LDR		rscratch2,[reg_cpu_var,#WaitCounter_ofs]
		TST		rscratch,#(IRQ_PENDING_FLAG|NMI_FLAG)
		MOVEQS		rscratch2,rscratch2
		BNE		5432f
@ CPU.WaitAddress = NULL; CPU.Cycles = CPU.NextEvent;
		bic		rstatus, rstatus, #MASK_SHUTDOWN
		LDR		reg_cycles,[reg_cpu_var,#NextEvent_ofs]
		B		5431f
5432:
@ else if (CPU.WaitCounter >= 2) CPU.WaitCounter = 1; else CPU.WaitCounter--;
		CMP		rscratch2,#2
		MOVHS		rscratch2,#1
		SUBLO		rscratch2,rscratch2,#1
		STR		rscratch2,[reg_cpu_var,#WaitCounter_ofs]
5431:
.endm						
.macro		BranchCheck0	
		/*in rsctach : OpAddress
		/*destroy rscratch2*/
		@ cpuops.cpp (SoundSkipMethod 0): if (CPU.BranchSkip) { CPU.BranchSkip = FALSE;
		@ if (CPU.PC - CPU.PCBase > OpAddress) return; } and the return comes
		@ before the operand byte's cycles, which asmRelative has added.
		tst	rstatus, #MASK_BRANCHSKIP
		bicne	rstatus, rstatus, #MASK_BRANCHSKIP
		subne	rscratch2, rpc, regpcbase
		cmpne	rscratch2, rscratch
		andhi	rscratch2, rstatus, #MEMSPEED_MASK
		subhi	reg_cycles, reg_cycles, rscratch2, lsr #MEMSPEED_SHIFT
		bhi	1111f
1110:
.endm									
.macro		BranchCheck1		
		/*in rsctach : OpAddress
		/*destroy rscratch2*/
		@ cpuops.cpp (SoundSkipMethod 0): if (CPU.BranchSkip) { CPU.BranchSkip = FALSE;
		@ if (CPU.PC - CPU.PCBase > OpAddress) return; } and the return comes
		@ before the operand byte's cycles, which asmRelative has added.
		tst	rstatus, #MASK_BRANCHSKIP
		bicne	rstatus, rstatus, #MASK_BRANCHSKIP
		subne	rscratch2, rpc, regpcbase
		cmpne	rscratch2, rscratch
		andhi	rscratch2, rstatus, #MEMSPEED_MASK
		subhi	reg_cycles, reg_cycles, rscratch2, lsr #MEMSPEED_SHIFT
		bhi	1111f
1110:
.endm												
.macro		BranchCheck2
		/*in rsctach : OpAddress
		/*destroy rscratch2*/
		@ cpuops.cpp (SoundSkipMethod 0): if (CPU.BranchSkip) { CPU.BranchSkip = FALSE;
		@ if (CPU.PC - CPU.PCBase > OpAddress) return; } and the return comes
		@ before the operand byte's cycles, which asmRelative has added.
		tst	rstatus, #MASK_BRANCHSKIP
		bicne	rstatus, rstatus, #MASK_BRANCHSKIP
		subne	rscratch2, rpc, regpcbase
		cmpne	rscratch2, rscratch
		andhi	rscratch2, rstatus, #MEMSPEED_MASK
		subhi	reg_cycles, reg_cycles, rscratch2, lsr #MEMSPEED_SHIFT
		bhi	1111f
1110:
.endm
			
.macro		S9xSetPCBase
		@  in  : rscratch (0x00hhmmll)
		mov	r3, pc		@ return address: PC reads as this instruction + 8,
		b	asmS9xSetPCBase	@ the one after the branch
.endm		

.macro		S9xFixCycles
		TST		rstatus,#MASK_EMUL
		LDRNE		rscratch, = jumptable0	   @ Emulation mode: M=1,X=1 and some C opcodes
		BNE		991111f
		@ EMULATION=0
		TST		rstatus,#MASK_MEM
		BEQ		991112f
		@ MEMORY=1
		TST		rstatus,#MASK_INDEX
		@ INDEX=1  @ Mode 0 : M=1,X=1
		LDRNE		rscratch, = jumptable1		
		@ INDEX=0  @ Mode 1 : M=1,X=0
		LDREQ		rscratch, = jumptable2
		B		991111f
991112:		@ MEMORY=0		
		TST		rstatus,#MASK_INDEX
		@ INDEX=1   @ Mode 3 : M=0,X=1
		LDRNE		rscratch, = jumptable4
		@ INDEX=0   @ Mode 2 : M=0,X=0
		LDREQ		rscratch, = jumptable3		
991111:
		STR		rscratch,[reg_cpu_var,#asm_OPTABLE_ofs]
.endm		
@ Runs opcode \op (already fetched) on the C core: asm_S9xRunCOpcode in
@ os9x_asm_cpu.cpp, for what the C core does differently in rare modes.
.macro		COPCODE op
		SAVE_REGS
		MOV	r0, #\op
		CCALL	asm_S9xRunCOpcode
		LOAD_REGS
		S9xFixCycles
.endm

.macro		S9xDoHBlankProcessing
		SAVE_REGS
		LDR	r0, [reg_cpu_var, #HBlank_ofs]	@ S9xDoHBlankProcessing_SFX or _NoSFX
		CCALLREG r0
		LOAD_REGS
.endm

/********************************/
.macro		EXEC_OP
.ifdef ASM_TRACE
		@ tests only: asm_trace (tests/asm/game_test.cpp) sees the ARM
		@ registers before every opcode, and leaves them as they were
		STMFD	sp!, {r0-r12, lr}
		MOV	r0, sp
		CCALL	asm_trace
		LDMFD	sp!, {r0-r12, lr}
.endif
		LDR		R1,[reg_cpu_var,#asm_OPTABLE_ofs]
		STR		rpc,[reg_cpu_var,#PCAtOpcodeStart_ofs]
		@ADD1MEM
		@ldr		r3, [reg_cpu_var, #MemSpeed_ofs]
		ldrb		r0, [rpc], #1		
		@add		reg_cycles, reg_cycles, r3
		and		r3, rstatus, #MEMSPEED_MASK			
		add		reg_cycles, reg_cycles, r3, lsr #MEMSPEED_SHIFT

		@LDRB		R0, [rpc], #1		
		
		LDR		PC, [R1,R0, LSL #2]
.endm
.ifdef ASM_SINGLE_STEP
.macro		NEXTOPCODE
		B			stepDone
		.pool
.endm
.else
.macro		NEXTOPCODE
		LDR			rscratch,[reg_cpu_var,#NextEvent_ofs]
		CMP			reg_cycles,rscratch
		BLT			mainLoop
		B			doHBlank	@ one copy, not one in every opcode
		.pool
.endm
.endif

@ Calls one of the memory routines in os9x_65c816_global.s, which return
@ with LDMFD R13!,{PC}. (psnes pushed the return address with STMFD R13!,{PC},
@ which stores the address 12 bytes on on an ARM9 but 8 on other ARM cores
@ and in qemu; ADR gives the same address everywhere.) Uses r3, which the
@ routines lose anyway.
.macro		CALLMEM func
		ADR	R3, 9999f
		STMFD	R13!,{R3}
		B	\func
9999:
.endm

@ #include "os9x_65c816_mac_mem.h"
.macro		S9xGetWord	
		@  in  : rscratch (0x00hhmmll)
		@  out : rscratch (0xhhll0000)
		CALLMEM	asmS9xGetWord
		MOV	R0, R0, LSL #16
.endm
.macro		S9xGetWordLow	
		@  in  : rscratch (0x00hhmmll)
		@  out : rscratch (0x0000hhll)		
		CALLMEM	asmS9xGetWord
.endm
.macro		S9xGetWordRegStatus	reg
		@  in  : rscratch (0x00hhmmll) 
		@  out : reg      (0xhhll0000)
		@  flags have to be updated with read value
		CALLMEM	asmS9xGetWord
		MOVS	\reg, R0, LSL #16
.endm
.macro		S9xGetWordRegNS	reg
		@  in  : rscratch (0x00hhmmll) 
		@  out : reg (0xhhll0000)
		@  DOES NOT DESTROY rscratch (R0)
		STMFD	R13!,{R0}
		CALLMEM	asmS9xGetWord
		MOV	\reg, R0, LSL #16
		LDMFD	R13!,{R0}
.endm			
.macro		S9xGetWordLowRegNS	reg
		@  in  : rscratch (0x00hhmmll) 
		@  out : reg (0xhhll0000)
		@  DOES NOT DESTROY rscratch (R0)
		STMFD	R13!,{R0}
		CALLMEM	asmS9xGetWord
		MOV	\reg, R0
		LDMFD	R13!,{R0}
.endm			

.macro		S9xGetByte 	
		@  in  : rscratch (0x00hhmmll)
		@  out : rscratch (0xll000000)
		CALLMEM	asmS9xGetByte
		MOV	R0, R0, LSL #24
.endm
.macro		S9xGetByteLow
		@  in  : rscratch (0x00hhmmll) 
		@  out : rscratch (0x000000ll)		
		CALLMEM	asmS9xGetByte
.endm
.macro		S9xGetByteRegStatus	reg
		@  in  : rscratch (0x00hhmmll)
		@  out : reg      (0xll000000)
		@  flags have to be updated with read value
		CALLMEM	asmS9xGetByte
		MOVS	\reg, R0, LSL #24
.endm
.macro		S9xGetByteRegNS	reg
		@  in  : rscratch (0x00hhmmll) 
		@  out : reg      (0xll000000)
		@  DOES NOT DESTROY rscratch (R0)
		STMFD	R13!,{R0}
		CALLMEM	asmS9xGetByte
		MOVS	\reg, R0, LSL #24
		LDMFD	R13!,{R0}
.endm
.macro		S9xGetByteLowRegNS	reg
		@  in  : rscratch (0x00hhmmll) 
		@  out : reg      (0x000000ll)
		@  DOES NOT DESTROY rscratch (R0)
		STMFD	R13!,{R0}
		CALLMEM	asmS9xGetByte
		MOVS	\reg, R0
		LDMFD	R13!,{R0}
.endm

@ Stack words (PullW / PushW in cpuops.cpp): see asmS9xGetStackWord
.macro		S9xGetStackWordLow
		@  in  : rscratch = S + 1
		@  out : rscratch (0x0000hhll)
		CALLMEM	asmS9xGetStackWord
.endm
.macro		S9xSetStackWordLow	regValue
		@  in  : regValue  (0x0000hhll)
		@  in  : rscratch = S - 1
		MOV	R1,\regValue
		CALLMEM	asmS9xSetStackWord
.endm
.macro		S9xSetStackWord	regValue
		@  in  : regValue  (0xhhll0000)
		@  in  : rscratch = S - 1
		MOV	R1,\regValue, LSR #16
		CALLMEM	asmS9xSetStackWord
.endm
@ S stays a 16-bit number (it wraps in bank 0)
.macro		WRAP_S
		MOV	reg_s, reg_s, LSL #16
		MOV	reg_s, reg_s, LSR #16
.endm

.macro		S9xSetWord	regValue		
		@  in  : regValue  (0xhhll0000)
		@  in  : rscratch=address   (0x00hhmmll)
		MOV	R1,\regValue, LSR #16
		CALLMEM	asmS9xSetWord
.endm
.macro		S9xSetWordZero	
		@  in  : rscratch=address   (0x00hhmmll)
		MOV	R1,#0
		CALLMEM	asmS9xSetWord
.endm
.macro		S9xSetWordLow	regValue		
		@  in  : regValue  (0x0000hhll)
		@  in  : rscratch=address   (0x00hhmmll)
		MOV	R1,\regValue
		CALLMEM	asmS9xSetWord
.endm
.macro		S9xSetByte	regValue
		@  in  : regValue  (0xll000000)
		@  in  : rscratch=address   (0x00hhmmll)
		MOV	R1,\regValue, LSR #24
		CALLMEM	asmS9xSetByte
.endm
.macro		S9xSetByteZero			
		@  in  : rscratch=address   (0x00hhmmll)
		MOV	R1,#0
		CALLMEM	asmS9xSetByte
.endm
.macro		S9xSetByteLow	regValue
		@  in  : regValue  (0x000000ll)
		@  in  : rscratch=address   (0x00hhmmll)
		MOV	R1,\regValue
		CALLMEM	asmS9xSetByte
.endm

.macro		doMainLoop
	@ save registers: ten words, so C calls start from an 8-byte aligned stack
	STMFD		R13!,{R4-R12,LR}
	@ init pointer to CPUvar structure
	MOV		reg_cpu_var,R0
	@ init registers
	LOAD_REGS
	@ get cpu mode from flag and init jump table
	S9xFixCycles

mainLoop:
	@ Test Flags
	LDR		rscratch,[reg_cpu_var,#Flags_ofs]
	MOVS		rscratch,rscratch
	BNE		CPUFlags_set	@ If flags => check for irq/nmi/scan_keys...

	EXEC_OP						@ Execute next opcode

CPUFlags_set:	@ Check flags (!=0), as the C main loops in cpuexec.cpp do
		TST	rscratch,#NMI_FLAG		@ Check NMI
		BEQ	CPUFlagsNMI_FLAG_cleared
		LDR	rscratch2,[reg_cpu_var,#NMICycleCount_ofs]
		SUBS	rscratch2,rscratch2,#1
		STR	rscratch2,[reg_cpu_var,#NMICycleCount_ofs]
		BNE	CPUFlagsNMI_FLAG_cleared
		BIC	rscratch,rscratch,#NMI_FLAG
		STR	rscratch,[reg_cpu_var,#Flags_ofs]
		LDRB	rscratch2,[reg_cpu_var,#WaitingForInterrupt_ofs]
		MOVS	rscratch2,rscratch2
		BEQ	NotCPUaitingForInterruptNMI
		MOV	rscratch2,#0
		ADD	rpc,rpc,#1
		STRB	rscratch2,[reg_cpu_var,#WaitingForInterrupt_ofs]
NotCPUaitingForInterruptNMI:
		S9xOpcode_NMI
		LDR	rscratch,[reg_cpu_var,#Flags_ofs]
CPUFlagsNMI_FLAG_cleared:
		TST	rscratch,#IRQ_PENDING_FLAG   @ Check IRQ_PENDING_FLAG
		BEQ	CPUFlagsIRQ_PENDING_FLAG_cleared
		LDR	rscratch2,[reg_cpu_var,#IRQCycleCount_ofs]
		MOVS	rscratch2,rscratch2
		BNE	CPUIRQCycleCount_NotZero
		LDRB	rscratch2,[reg_cpu_var,#WaitingForInterrupt_ofs]
		MOVS	rscratch2,rscratch2
		BEQ	NotCPUaitingForInterruptIRQ
		MOV	rscratch2,#0
		ADD	rpc,rpc,#1
		STRB	rscratch2,[reg_cpu_var,#WaitingForInterrupt_ofs]
NotCPUaitingForInterruptIRQ:
		@ (Settings.DisableIRQ, which nothing sets, isn't looked at)
		LDRB	rscratch2,[reg_cpu_var,#IRQActive_ofs]
		MOVS	rscratch2,rscratch2
		BEQ	CPUIRQActive_cleared
		TST	rstatus,#MASK_IRQ
		BNE	CPUFlagsIRQ_PENDING_FLAG_cleared
		S9xOpcode_IRQ
		LDR	rscratch,[reg_cpu_var,#Flags_ofs]
		B	CPUFlagsIRQ_PENDING_FLAG_cleared
CPUIRQActive_cleared:
		BIC	rscratch,rscratch,#IRQ_PENDING_FLAG
		STR	rscratch,[reg_cpu_var,#Flags_ofs]
		B	CPUFlagsIRQ_PENDING_FLAG_cleared
CPUIRQCycleCount_NotZero:
		@ if (--CPU.IRQCycleCount == 0 && CheckFlag (IRQ)) CPU.IRQCycleCount = 1;
		SUBS	rscratch2,rscratch2,#1
		BNE	1f
		TST	rstatus,#MASK_IRQ
		MOVNE	rscratch2,#1
1:		STR	rscratch2,[reg_cpu_var,#IRQCycleCount_ofs]
CPUFlagsIRQ_PENDING_FLAG_cleared:

		TST	rscratch,#SCAN_KEYS_FLAG   @ Check SCAN_KEYS_FLAG
		BNE	endmainLoop

	EXEC_OP	@ Execute next opcode

endmainLoop:
	SAVE_REGS
	LDMFD		R13!,{R4-R12,pc}

doHBlank:
	S9xDoHBlankProcessing
	B		mainLoop
.endm

@ For tests: one opcode, without the interrupt checks or HBlank processing
@ (NEXTOPCODE comes back to stepDone when ASM_SINGLE_STEP is set).
.macro		doStepOpcode
	STMFD		R13!,{R4-R12,LR}
	MOV		reg_cpu_var,R0
	LOAD_REGS
	S9xFixCycles
	EXEC_OP
stepDone:
	@ The bits of the ARM registers that must be zero, which SAVE_REGS
	@ doesn't show: CPU.AsmStatus gets a bit for each register that has
	@ some set (the opcode tests look at it).
	mov	r0, #0
	tst	rstatus, #MASK_MEM
	movne	r1, reg_a, lsl #8		@ 8-bit A: 0xll000000
	moveq	r1, reg_a, lsl #16		@ 16-bit A: 0xhhll0000
	cmp	r1, #0
	orrne	r0, r0, #1
	tst	rstatus, #MASK_INDEX
	movne	r1, reg_x, lsl #8
	moveq	r1, reg_x, lsl #16
	cmp	r1, #0
	orrne	r0, r0, #2
	tst	rstatus, #MASK_INDEX
	movne	r1, reg_y, lsl #8
	moveq	r1, reg_y, lsl #16
	cmp	r1, #0
	orrne	r0, r0, #4
	tst	reg_d, #0xFF00			@ 0xhhll00pb
	orrne	r0, r0, #8
	movs	r1, reg_s, lsr #16		@ 0x0000hhll
	orrne	r0, r0, #16
	tst	rstatus, #0xFF00		@ between DB and the memory speed
	orrne	r0, r0, #32
	tst	rstatus, #MASK_APU_EXECUTING
	orrne	r0, r0, #64
	str	r0, [reg_cpu_var, #rstatus_ofs]
	SAVE_REGS
	LDMFD		R13!,{R4-R12,pc}
.endm
