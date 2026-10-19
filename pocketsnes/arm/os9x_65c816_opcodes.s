@  ===========================================
@  ===========================================
@  Adressing mode
@  ===========================================
@  ===========================================


.macro		Absolute cyc=1, rd=0
		.if \cyc
		ADD2MEM		
		.endif
		LDRB	rscratch2    , [rpc, #1]
		.if \rd
		STRB	rscratch2, [reg_cpu_var, #OpenBus_ofs]
		.endif
		LDRB	rscratch   , [rpc],#2
		ORR	rscratch    , rscratch,	rscratch2, LSL #8
		ORR	rscratch    , rscratch,	reg_d_bank, LSL #16
.endm
.macro		AbsoluteIndexedIndirectX0
		ADD2MEM		
		LDRB	rscratch2    , [rpc, #1]
		STRB	rscratch2, [reg_cpu_var, #OpenBus_ofs]
		LDRB	rscratch   , [rpc], #2
		ORR	rscratch    , rscratch,	rscratch2, LSL #8
		ADD	rscratch    , reg_x, rscratch, LSL #16
		MOV	rscratch , rscratch, LSR #16
		ORR	rscratch    , rscratch,	reg_p_bank, LSL #16
		S9xGetWordLow
		
.endm
.macro		AbsoluteIndexedIndirectX1
		ADD2MEM		
		LDRB	rscratch2    , [rpc, #1]
		STRB	rscratch2, [reg_cpu_var, #OpenBus_ofs]
		LDRB	rscratch   , [rpc], #2
		ORR	rscratch    , rscratch,	rscratch2, LSL #8
		ADD	rscratch    , rscratch, reg_x, LSR #24
		BIC	rscratch , rscratch, #0x00FF0000
		ORR	rscratch    , rscratch,	reg_p_bank, LSL #16
		S9xGetWordLow
		
.endm
.macro		AbsoluteIndirectLong		
		ADD2MEM
		LDRB			rscratch2    , [rpc, #1]
		STRB	rscratch2, [reg_cpu_var, #OpenBus_ofs]
		LDRB			rscratch   , [rpc], #2
		ORR			rscratch    , rscratch,	rscratch2, LSL #8
		S9xGetWordLowRegNS 	rscratch2
		ADD			rscratch   , rscratch,	#2
		STMFD			r13!,{rscratch2}
		S9xGetByteLow
		LDMFD			r13!,{rscratch2}
		ORR			rscratch   , rscratch2, rscratch, LSL #16
.endm
.macro		AbsoluteIndirect
		ADD2MEM
		LDRB	rscratch2    , [rpc,#1]
		STRB	rscratch2, [reg_cpu_var, #OpenBus_ofs]
		LDRB	rscratch   , [rpc], #2
		ORR	rscratch    , rscratch,	rscratch2, LSL #8
		S9xGetWordLow
		ORR	rscratch    , rscratch,	reg_p_bank, LSL #16
.endm
.macro		AbsoluteIndexedX0 cyc=1, rd=0
		.if \cyc
		ADD2MEM
		.endif
		LDRB	rscratch2    , [rpc, #1]
		.if \rd
		STRB	rscratch2, [reg_cpu_var, #OpenBus_ofs]
		.endif
		LDRB	rscratch   , [rpc], #2
		ORR	rscratch    , rscratch,	rscratch2, LSL #8
		ORR	rscratch    , rscratch,	reg_d_bank, LSL #16
		ADD	rscratch    , rscratch,	reg_x, LSR #16
.endm
.macro		AbsoluteIndexedX1 cyc=1, rd=0
		.if \cyc
		ADD2MEM
		.endif
		LDRB	rscratch2    , [rpc, #1]
		.if \rd
		STRB	rscratch2, [reg_cpu_var, #OpenBus_ofs]
		.endif
		LDRB	rscratch   , [rpc], #2
		ORR	rscratch    , rscratch,	rscratch2, LSL #8
		ORR	rscratch    , rscratch,	reg_d_bank, LSL #16
		ADD	rscratch    , rscratch,	reg_x, LSR #24
.endm


.macro		AbsoluteIndexedY0 cyc=1, rd=0
		.if \cyc
		ADD2MEM
		.endif
		LDRB	rscratch2    , [rpc, #1]
		.if \rd
		STRB	rscratch2, [reg_cpu_var, #OpenBus_ofs]
		.endif
		LDRB	rscratch   , [rpc], #2
		ORR	rscratch    , rscratch,	rscratch2, LSL #8
		ORR	rscratch    , rscratch,	reg_d_bank, LSL #16
		ADD	rscratch    , rscratch,	reg_y, LSR #16
.endm
.macro		AbsoluteIndexedY1 cyc=1, rd=0
		.if \cyc
		ADD2MEM
		.endif
		LDRB	rscratch2    , [rpc, #1]
		.if \rd
		STRB	rscratch2, [reg_cpu_var, #OpenBus_ofs]
		.endif
		LDRB	rscratch   , [rpc], #2
		ORR	rscratch    , rscratch,	rscratch2, LSL #8
		ORR	rscratch    , rscratch,	reg_d_bank, LSL #16
		ADD	rscratch    , rscratch,	reg_y, LSR #24
.endm
.macro		AbsoluteLong cyc=1, rd=0
		.if \cyc
		ADD3MEM
		.endif
		LDRB	rscratch2    , [rpc, #1]
		LDRB	rscratch   , [rpc], #2
		ORR	rscratch    , rscratch,	rscratch2, LSL #8
		LDRB	rscratch2   , [rpc], #1
		.if \rd
		STRB	rscratch2, [reg_cpu_var, #OpenBus_ofs]
		.endif
		ORR	rscratch    , rscratch,	rscratch2, LSL #16
.endm


.macro		AbsoluteLongIndexedX0 cyc=1, rd=0
		.if \cyc
		ADD3MEM
		.endif
		LDRB	rscratch2    , [rpc, #1]
		LDRB	rscratch   , [rpc], #2
		ORR	rscratch    , rscratch,	rscratch2, LSL #8
		LDRB	rscratch2   , [rpc], #1
		.if \rd
		STRB	rscratch2, [reg_cpu_var, #OpenBus_ofs]
		.endif
		ORR	rscratch    , rscratch,	rscratch2, LSL #16
		ADD	rscratch    , rscratch,	reg_x, LSR #16
		BIC	rscratch, rscratch, #0xFF000000
.endm
.macro		AbsoluteLongIndexedX1 cyc=1, rd=0
		.if \cyc
		ADD3MEM
		.endif
		LDRB	rscratch2    , [rpc, #1]
		LDRB	rscratch   , [rpc], #2
		ORR	rscratch    , rscratch,	rscratch2, LSL #8
		LDRB	rscratch2   , [rpc], #1
		.if \rd
		STRB	rscratch2, [reg_cpu_var, #OpenBus_ofs]
		.endif
		ORR	rscratch    , rscratch,	rscratch2, LSL #16
		ADD	rscratch    , rscratch,	reg_x, LSR #24
		BIC	rscratch, rscratch, #0xFF000000		
.endm
.macro		Direct cyc=1, rd=0
		.if \cyc
		ADD1MEM
		.endif
		LDRB	rscratch    , [rpc], #1
		.if \rd
		STRB	rscratch, [reg_cpu_var, #OpenBus_ofs]
		.endif
		ADD	rscratch    , reg_d, rscratch, LSL #16
		MOV	rscratch, rscratch, LSR #16
.endm
.macro		DirectIndirect cyc=1, rd=0
		.if \cyc
		ADD1MEM
		.endif
		LDRB	rscratch    , [rpc], #1
		STRB	rscratch, [reg_cpu_var, #OpenBus_ofs]
		ADD	rscratch    , reg_d, rscratch,	 LSL #16		
		MOV	rscratch, rscratch, LSR #16
		S9xGetWordLow
		.if \rd
		MOV	rscratch2, rscratch, LSR #8
		STRB	rscratch2, [reg_cpu_var, #OpenBus_ofs]
		.endif
		ORR	rscratch    , rscratch,	reg_d_bank, LSL #16
.endm
.macro		DirectIndirectLong cyc=1, rd=0
		.if \cyc
		ADD1MEM
		.endif
		LDRB			rscratch    , [rpc], #1
		STRB	rscratch, [reg_cpu_var, #OpenBus_ofs]
		ADD			rscratch    , reg_d, rscratch,	 LSL #16
		MOV			rscratch, rscratch, LSR #16		
		S9xGetWordLowRegNS	rscratch2
		ADD			rscratch    , rscratch,#2
		STMFD			r13!,{rscratch2}
		S9xGetByteLow
		.if \rd
		STRB	rscratch, [reg_cpu_var, #OpenBus_ofs]
		.endif
		LDMFD			r13!,{rscratch2}
		ORR			rscratch   , rscratch2, rscratch, LSL #16
.endm
.macro		DirectIndirectIndexed0 cyc=1, rd=0
		.if \cyc
		ADD1MEM
		.endif
		LDRB	rscratch    , [rpc], #1
		STRB	rscratch, [reg_cpu_var, #OpenBus_ofs]
		ADD	rscratch    , reg_d, rscratch,	 LSL #16
		MOV	rscratch, rscratch, LSR #16
		S9xGetWordLow
		.if \rd
		MOV	rscratch2, rscratch, LSR #8
		STRB	rscratch2, [reg_cpu_var, #OpenBus_ofs]
		.endif
		ORR	rscratch, rscratch,reg_d_bank, LSL #16
		ADD	rscratch, rscratch,reg_y, LSR #16
.endm
.macro		DirectIndirectIndexed1 cyc=1, rd=0
		.if \cyc
		ADD1MEM
		.endif
		LDRB	rscratch    , [rpc], #1
		STRB	rscratch, [reg_cpu_var, #OpenBus_ofs]
		ADD	rscratch    , reg_d, rscratch,	 LSL #16
		MOV	rscratch, rscratch, LSR #16
		S9xGetWordLow
		.if \rd
		MOV	rscratch2, rscratch, LSR #8
		STRB	rscratch2, [reg_cpu_var, #OpenBus_ofs]
		.endif
		ORR	rscratch, rscratch,reg_d_bank, LSL #16
		ADD	rscratch, rscratch,reg_y, LSR #24
.endm
.macro		DirectIndirectIndexedLong0 cyc=1, rd=0
		.if \cyc
		ADD1MEM
		.endif
		LDRB			rscratch    , [rpc], #1
		STRB	rscratch, [reg_cpu_var, #OpenBus_ofs]
		ADD			rscratch    , reg_d, rscratch,	 LSL #16
		MOV			rscratch, rscratch, LSR #16		
		S9xGetWordLowRegNS	rscratch2
		ADD			rscratch    , rscratch,#2
		STMFD			r13!,{rscratch2}
		S9xGetByteLow
		.if \rd
		STRB	rscratch, [reg_cpu_var, #OpenBus_ofs]
		.endif
		LDMFD			r13!,{rscratch2}
		ORR			rscratch   , rscratch2, rscratch, LSL #16				

		ADD			rscratch, rscratch,reg_y, LSR #16
.endm
.macro		DirectIndirectIndexedLong1 cyc=1, rd=0
		.if \cyc
		ADD1MEM
		.endif
		LDRB			rscratch    , [rpc], #1
		STRB	rscratch, [reg_cpu_var, #OpenBus_ofs]
		ADD			rscratch    , reg_d, rscratch,	 LSL #16
		MOV			rscratch, rscratch, LSR #16
		S9xGetWordLowRegNS	rscratch2
		ADD			rscratch    , rscratch,#2
		STMFD			r13!,{rscratch2}
		S9xGetByteLow
		.if \rd
		STRB	rscratch, [reg_cpu_var, #OpenBus_ofs]
		.endif
		LDMFD			r13!,{rscratch2}
		ORR			rscratch   , rscratch2, rscratch, LSL #16
		ADD			rscratch, rscratch,reg_y, LSR #24
.endm
.macro		DirectIndexedIndirect0 cyc=1, rd=0
		.if \cyc
		ADD1MEM		@ cpuops.cpp: CPU.MemSpeed for the operand, no index cycle
		.endif
		LDRB	rscratch    , [rpc], #1				
		STRB	rscratch, [reg_cpu_var, #OpenBus_ofs]
		ADD	rscratch2   , reg_d , reg_x
		ADD	rscratch    , rscratch2 , rscratch, LSL #16		
		MOV	rscratch, rscratch, LSR #16
		S9xGetWordLow
		.if \rd
		MOV	rscratch2, rscratch, LSR #8
		STRB	rscratch2, [reg_cpu_var, #OpenBus_ofs]
		.endif
		ORR	rscratch    , rscratch , reg_d_bank, LSL #16		
.endm
.macro		DirectIndexedIndirect1 cyc=1, rd=0
		.if \cyc
		ADD1MEM		@ cpuops.cpp: CPU.MemSpeed for the operand, no index cycle
		.endif
		LDRB	rscratch    , [rpc], #1
		STRB	rscratch, [reg_cpu_var, #OpenBus_ofs]
		ADD	rscratch2   , reg_d , reg_x, LSR #8
		ADD	rscratch    , rscratch2 , rscratch, LSL #16
		MOV	rscratch, rscratch, LSR #16
		S9xGetWordLow
		.if \rd
		MOV	rscratch2, rscratch, LSR #8
		STRB	rscratch2, [reg_cpu_var, #OpenBus_ofs]
		.endif
		ORR	rscratch    , rscratch , reg_d_bank, LSL #16		
.endm
.macro		DirectIndexedX0 cyc=1, rd=0
		.if \cyc
		ADD1CYCLE1MEM
		.endif
		LDRB	rscratch    , [rpc], #1
		.if \rd
		STRB	rscratch, [reg_cpu_var, #OpenBus_ofs]
		.endif
		ADD	rscratch2   , reg_d , reg_x
		ADD	rscratch    , rscratch2 , rscratch, LSL #16
		MOV	rscratch, rscratch, LSR #16
.endm
.macro		DirectIndexedX1 cyc=1, rd=0
		.if \cyc
		ADD1CYCLE1MEM
		.endif
		LDRB	rscratch    , [rpc], #1
		.if \rd
		STRB	rscratch, [reg_cpu_var, #OpenBus_ofs]
		.endif
		ADD	rscratch2   , reg_d , reg_x, LSR #8
		ADD	rscratch    , rscratch2 , rscratch, LSL #16
		MOV	rscratch, rscratch, LSR #16
		TST	rstatus, #MASK_EMUL		@ cpuaddr.h: Addr &= CheckEmulation() ? 0xff : 0xffff;
		ANDNE	rscratch, rscratch, #0xFF
.endm
.macro		DirectIndexedY0 cyc=1, rd=0
		.if \cyc
		ADD1CYCLE1MEM
		.endif
		LDRB	rscratch    , [rpc], #1
		.if \rd
		STRB	rscratch, [reg_cpu_var, #OpenBus_ofs]
		.endif
		ADD	rscratch2   , reg_d , reg_y
		ADD	rscratch    , rscratch2 , rscratch, LSL #16
		MOV	rscratch, rscratch, LSR #16
.endm
.macro		DirectIndexedY1 cyc=1, rd=0
		.if \cyc
		ADD1CYCLE1MEM
		.endif
		LDRB	rscratch    , [rpc], #1
		.if \rd
		STRB	rscratch, [reg_cpu_var, #OpenBus_ofs]
		.endif
		ADD	rscratch2   , reg_d , reg_y, LSR #8
		ADD	rscratch    , rscratch2 , rscratch, LSL #16
		MOV	rscratch, rscratch, LSR #16
		TST	rstatus, #MASK_EMUL		@ cpuaddr.h: Addr &= CheckEmulation() ? 0xff : 0xffff;
		ANDNE	rscratch, rscratch, #0xFF
.endm
.macro		Immediate8
		ADD	rscratch, rpc, reg_p_bank, LSL #16
		SUB	rscratch, rscratch, regpcbase
		ADD	rpc, rpc, #1
.endm
.macro		Immediate16
		ADD	rscratch, rpc, reg_p_bank, LSL #16
		SUB	rscratch, rscratch, regpcbase
		ADD	rpc, rpc, #2
.endm
.macro		asmRelative
		ADD1MEM
		LDRSB	rscratch    , [rpc],#1
		ADD	rscratch , rscratch , rpc
		SUB	rscratch , rscratch, regpcbase		
		BIC	rscratch,rscratch,#0x00FF0000
		BIC	rscratch,rscratch,#0xFF000000
.endm
.macro		asmRelativeLong
		ADD1CYCLE2MEM
		LDRB	rscratch2    , [rpc, #1]
		LDRB	rscratch   , [rpc], #2
		ORR	rscratch    , rscratch,	rscratch2, LSL #8
		SUB	rscratch2    , rpc, regpcbase
		ADD	rscratch    , rscratch2, rscratch		
		BIC	rscratch,rscratch,#0x00FF0000
.endm


.macro		StackasmRelative cyc=1, rd=0
		.if \cyc
		ADD1CYCLE1MEM
		.endif
		LDRB	rscratch    , [rpc], #1
		.if \rd
		STRB	rscratch, [reg_cpu_var, #OpenBus_ofs]
		.endif
		ADD	rscratch    , rscratch,	reg_s
		BIC	rscratch,rscratch,#0x00FF0000
.endm
.macro		StackasmRelativeIndirectIndexed0 cyc=1, rd=0
		.if \cyc
		ADD2CYCLE1MEM
		.endif
		LDRB	rscratch    , [rpc], #1
		STRB	rscratch, [reg_cpu_var, #OpenBus_ofs]
		ADD	rscratch    , rscratch,	reg_s
		BIC	rscratch,rscratch,#0x00FF0000
		S9xGetWordLow
		.if \rd
		MOV	rscratch2, rscratch, LSR #8
		STRB	rscratch2, [reg_cpu_var, #OpenBus_ofs]
		.endif
		ORR	rscratch    , rscratch,	reg_d_bank, LSL #16
		ADD	rscratch    , rscratch,	reg_y, LSR #16
		BIC	rscratch, rscratch, #0xFF000000
.endm
.macro		StackasmRelativeIndirectIndexed1 cyc=1, rd=0
		.if \cyc
		ADD2CYCLE1MEM
		.endif
		LDRB	rscratch    , [rpc], #1
		STRB	rscratch, [reg_cpu_var, #OpenBus_ofs]
		ADD	rscratch    , rscratch,	reg_s
		BIC	rscratch,rscratch,#0x00FF0000
		S9xGetWordLow
		.if \rd
		MOV	rscratch2, rscratch, LSR #8
		STRB	rscratch2, [reg_cpu_var, #OpenBus_ofs]
		.endif
		ORR	rscratch    , rscratch,	reg_d_bank, LSL #16
		ADD	rscratch    , rscratch,	reg_y, LSR #24
		BIC	rscratch, rscratch, #0xFF000000
.endm


/****************************************/
@ The stack as cpuops.cpp's PushB/PushW/PullB/PullW: S is 16 bits and
@ wraps in bank 0, words go through asmS9xGetStackWord/SetStackWord.

.macro		PushB		reg
		MOV		rscratch,reg_s
		S9xSetByte	\reg
		SUB		reg_s,reg_s,#1
		WRAP_S
.endm
.macro		PushBLow	reg
		MOV		rscratch,reg_s
		S9xSetByteLow	\reg
		SUB		reg_s,reg_s,#1
		WRAP_S
.endm
.macro		PushWLow	reg 
		SUB		rscratch,reg_s,#1
		S9xSetStackWordLow	\reg
		SUB		reg_s,reg_s,#2
		WRAP_S
.endm
.macro		PushWrLow	
		MOV		rscratch2,rscratch
		SUB		rscratch,reg_s,#1
		S9xSetStackWordLow	rscratch2
		SUB		reg_s,reg_s,#2
		WRAP_S
.endm
.macro		PushW		reg
		SUB		rscratch,reg_s,#1
		S9xSetStackWord	\reg
		SUB		reg_s,reg_s,#2
		WRAP_S
.endm
.macro		PullB		reg
		ADD		reg_s,reg_s,#1
		WRAP_S
		MOV		rscratch,reg_s
		S9xGetByteLow
		MOV		\reg,rscratch,LSL #24
.endm
.macro		PullBr		
		ADD		reg_s,reg_s,#1
		WRAP_S
		MOV		rscratch,reg_s
		S9xGetByte
.endm
.macro		PullBLow	reg
		ADD		reg_s,reg_s,#1
		WRAP_S
		MOV		rscratch,reg_s
		S9xGetByteLow
		MOV		\reg,rscratch
.endm
.macro		PullBrLow
		ADD		reg_s,reg_s,#1
		WRAP_S
		MOV		rscratch,reg_s
		S9xGetByteLow
.endm
.macro		PullW		reg
		ADD		rscratch,reg_s,#1
		S9xGetStackWordLow
		ADD		reg_s,reg_s,#2
		WRAP_S
		MOV		\reg,rscratch,LSL #16
.endm
.macro		PullWLow	reg
		ADD		rscratch,reg_s,#1
		S9xGetStackWordLow
		ADD		reg_s,reg_s,#2
		WRAP_S
		MOV		\reg,rscratch
.endm
.macro		PullBS		reg
		ADD		reg_s,reg_s,#1
		WRAP_S
		MOV		rscratch,reg_s
		S9xGetByteLow
		MOVS		\reg,rscratch,LSL #24
.endm
.macro		PullBrS	
		ADD		reg_s,reg_s,#1
		WRAP_S
		MOV		rscratch,reg_s
		S9xGetByteLow
		MOVS		rscratch,rscratch,LSL #24
.endm
.macro		PullBLowS	reg
		ADD		reg_s,reg_s,#1
		WRAP_S
		MOV		rscratch,reg_s
		S9xGetByteLow
		MOVS		\reg,rscratch
.endm
.macro		PullBrLowS	
		ADD		reg_s,reg_s,#1
		WRAP_S
		MOV		rscratch,reg_s
		S9xGetByteLow
		MOVS		rscratch,rscratch
.endm
.macro		PullWS		reg
		ADD		rscratch,reg_s,#1
		S9xGetStackWordLow
		ADD		reg_s,reg_s,#2
		WRAP_S
		MOVS		\reg,rscratch, LSL #16
.endm
.macro		PullWrS		
		ADD		rscratch,reg_s,#1
		S9xGetStackWordLow
		ADD		reg_s,reg_s,#2
		WRAP_S
		MOVS		rscratch,rscratch, LSL #16
.endm
.macro		PullWLowS	reg
		ADD		rscratch,reg_s,#1
		S9xGetStackWordLow
		ADD		reg_s,reg_s,#2
		WRAP_S
		MOVS		\reg,rscratch
.endm
.macro		PullWrLowS	
		ADD		rscratch,reg_s,#1
		S9xGetStackWordLow
		ADD		reg_s,reg_s,#2
		WRAP_S
		MOVS		rscratch,rscratch
.endm


/*****************************************************************
	FLAGS  
*****************************************************************/

.macro		UPDATE_C
		@  CC : ARM Carry Clear
		BICCC	rstatus, rstatus, #MASK_CARRY  @ 	0 : AND	mask 11111011111 : set C to zero
		@  CS : ARM Carry Set
		ORRCS	rstatus, rstatus, #MASK_CARRY      @ 	1 : OR	mask 00000100000 : set C to one
.endm
.macro		UPDATE_Z
		@  NE : ARM Zero Clear
		BICNE	rstatus, rstatus, #MASK_ZERO	 @  0 : AND mask	11111011111 : set Z to zero
		@  EQ : ARM Zero Set
		ORREQ	rstatus, rstatus, #MASK_ZERO	 @  1 : OR  mask	00000100000  : set Z to	one		
.endm
.macro		UPDATE_ZN
		@  NE : ARM Zero Clear
		BICNE	rstatus, rstatus, #MASK_ZERO	 @  0 : AND mask	11111011111 : set Z to zero
		@  EQ : ARM Zero Set
		ORREQ	rstatus, rstatus, #MASK_ZERO	 @  1 : OR  mask	00000100000  : set Z to	one
		@  PL : ARM Neg Clear
		BICPL	rstatus, rstatus, #MASK_NEG	@  0 : AND mask 11111011111 : set N to zero
		@  MI : ARM Neg Set
		ORRMI	rstatus, rstatus, #MASK_NEG	@  1 : OR  mask 00000100000 : set N to one
.endm

/*****************************************************************
	OPCODES_MAC
*****************************************************************/




.macro ADC8
		TST rstatus, #MASK_DECIMAL
		BEQ 1111f				
		@ decimal mode: the C core's arithmetic (os9x_asm_cpu.cpp)
		S9xGetByteLow
		MOV	r1, r0
		MOV	r0, reg_a, LSR #24
		TST	rstatus, #MASK_CARRY
		MOVEQ	r2, #0
		MOVNE	r2, #1
		CCALL	asm_DecimalADC8
		TST	r0, #0x100
		ORRNE	rstatus, rstatus, #MASK_CARRY
		BICEQ	rstatus, rstatus, #MASK_CARRY
		TST	r0, #0x200
		ORRNE	rstatus, rstatus, #MASK_OVERFLOW
		BICEQ	rstatus, rstatus, #MASK_OVERFLOW
		MOVS	reg_a, r0, LSL #24
		UPDATE_ZN
		B	1113f
1111:
		S9xGetByteLow
		MOVS rscratch2, rstatus, LSR #MASK_SHIFTER_CARRY
		SUBCS rscratch, rscratch, #0x100 
		ADCS reg_a, reg_a, rscratch, ROR #8
		@ OverFlow
		ORRVS rstatus, rstatus, #MASK_OVERFLOW
		BICVC rstatus, rstatus, #MASK_OVERFLOW
		@ Carry
		UPDATE_C
		@ clear lower part
		ANDS reg_a, reg_a, #0xFF000000
		@ Update flag
		UPDATE_ZN
1113: 
.endm
/* TO TEST */
.macro ADC16 
		TST rstatus, #MASK_DECIMAL
		BEQ 1111f 
		@ decimal mode: the C core's arithmetic (os9x_asm_cpu.cpp)
		S9xGetWordLow
		MOV	r1, r0
		MOV	r0, reg_a, LSR #16
		TST	rstatus, #MASK_CARRY
		MOVEQ	r2, #0
		MOVNE	r2, #1
		CCALL	asm_DecimalADC16
		TST	r0, #0x10000
		ORRNE	rstatus, rstatus, #MASK_CARRY
		BICEQ	rstatus, rstatus, #MASK_CARRY
		TST	r0, #0x20000
		ORRNE	rstatus, rstatus, #MASK_OVERFLOW
		BICEQ	rstatus, rstatus, #MASK_OVERFLOW
		MOVS	reg_a, r0, LSL #16
		UPDATE_ZN
		B	1113f
1111:
		S9xGetWordLow
		MOVS rscratch2, rstatus, LSR #MASK_SHIFTER_CARRY 
		SUBCS rscratch, rscratch, #0x10000 
		ADCS reg_a, reg_a,rscratch, ROR #16
		@ OverFlow 
		ORRVS rstatus, rstatus, #MASK_OVERFLOW
		BICVC rstatus, rstatus, #MASK_OVERFLOW
		MOV reg_a, reg_a, LSR #16
		@ Carry
		UPDATE_C
		@ clear lower parts 
		MOVS reg_a, reg_a, LSL #16
		@ Update flag
		UPDATE_ZN
1113: 
.endm


.macro		AND16
		S9xGetWord
		ANDS		reg_a, reg_a, rscratch
		UPDATE_ZN
.endm
.macro		AND8
		S9xGetByte
		ANDS		reg_a, reg_a, rscratch
		UPDATE_ZN
.endm
.macro		A_ASL8
		@  7	instr		
		MOVS	reg_a, reg_a, LSL #1
		UPDATE_C
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		A_ASL16
		@  7	instr		
		MOVS	reg_a, reg_a, LSL #1
		UPDATE_C
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		ASL16		
		S9xGetWordRegNS	rscratch2	      @ 	do not destroy Opadress	in rscratch
		MOVS		rscratch2, rscratch2, LSL #1
		UPDATE_C
		UPDATE_ZN		
		S9xSetWord 	rscratch2
		ADD1CYCLE
.endm
.macro		ASL8				
		S9xGetByteRegNS	rscratch2	      @ 	do not destroy Opadress	in rscratch
		MOVS		rscratch2, rscratch2, LSL #1
		UPDATE_C
		UPDATE_ZN		
		S9xSetByte 	rscratch2
		ADD1CYCLE
.endm
.macro		BIT8
		S9xGetByte
		MOVS	rscratch2, rscratch, LSL #1
		@  Trick in ASM : shift one more bit	: ARM C	= Snes N
		@ 					  ARM N	= Snes V
		@  If Carry Set, then Set Neg in SNES
		BICCC	rstatus, rstatus, #MASK_NEG	@  0 : AND mask 11111011111 : set C to zero
		ORRCS	rstatus, rstatus, #MASK_NEG	@  1 : OR  mask 00000100000 : set C to one
		@  If Neg Set, then Set Overflow in SNES
		BICPL	rstatus, rstatus, #MASK_OVERFLOW  @  0 : AND mask 11111011111	: set N	to zero
		ORRMI	rstatus, rstatus, #MASK_OVERFLOW	     @  1 : OR  mask 00000100000	: set N	to one

		@  Now do a real AND	with A register
		@  Set Zero Flag, bit test
		ANDS	rscratch2, reg_a, rscratch
		BICNE	rstatus, rstatus, #MASK_ZERO	 @  0 : AND mask	11111011111 : set Z to zero
		ORREQ	rstatus, rstatus, #MASK_ZERO	 @  1 : OR  mask	00000100000  : set Z to	one
.endm

.macro		BIT16
		S9xGetWord
		MOVS	rscratch2, rscratch, LSL #1
		@  Trick in ASM : shift one more bit	: ARM C	= Snes N
		@ 					  ARM N	= Snes V
		@  If Carry Set, then Set Neg in SNES
		BICCC	rstatus, rstatus, #MASK_NEG	@  0 : AND mask 11111011111 : set N to zero
		ORRCS	rstatus, rstatus, #MASK_NEG	@  1 : OR  mask 00000100000 : set N to one
		@  If Neg Set, then Set Overflow in SNES
		BICPL	rstatus, rstatus, #MASK_OVERFLOW  @  0 : AND mask 11111011111	: set V	to zero
		ORRMI	rstatus, rstatus, #MASK_OVERFLOW	     @  1 : OR  mask 00000100000	: set V	to one
		@  Now do a real AND	with A register
		@  Set Zero Flag, bit test
		ANDS	rscratch2, reg_a, rscratch
		@  Bit set  ->Z=0->xxxNE Clear flag
		BICNE	rstatus, rstatus, #MASK_ZERO	 @  0 : AND mask	11111011111 : set Z to zero
		@  Bit clear->Z=1->xxxEQ Set flag
		ORREQ	rstatus, rstatus, #MASK_ZERO	 @  1 : OR  mask	00000100000  : set Z to	one
.endm
.macro		CMP8
		S9xGetByte			
		SUBS 	rscratch2,reg_a,rscratch		
		BICCC	rstatus, rstatus, #MASK_CARRY
		ORRCS	rstatus, rstatus, #MASK_CARRY
		UPDATE_ZN
		
.endm
.macro		CMP16
		S9xGetWord
		SUBS 	rscratch2,reg_a,rscratch		
		BICCC	rstatus, rstatus, #MASK_CARRY
		ORRCS	rstatus, rstatus, #MASK_CARRY
		UPDATE_ZN
		
.endm
.macro		CMX16
		S9xGetWord
		SUBS 	rscratch2,reg_x,rscratch		
		BICCC	rstatus, rstatus, #MASK_CARRY
		ORRCS	rstatus, rstatus, #MASK_CARRY
		UPDATE_ZN
.endm
.macro		CMX8
		S9xGetByte
		SUBS 	rscratch2,reg_x,rscratch		
		BICCC	rstatus, rstatus, #MASK_CARRY
		ORRCS	rstatus, rstatus, #MASK_CARRY
		UPDATE_ZN
.endm
.macro		CMY16
		S9xGetWord
		SUBS 	rscratch2,reg_y,rscratch		
		BICCC	rstatus, rstatus, #MASK_CARRY
		ORRCS	rstatus, rstatus, #MASK_CARRY
		UPDATE_ZN
.endm
.macro		CMY8
		S9xGetByte
		SUBS 	rscratch2,reg_y,rscratch		
		BICCC	rstatus, rstatus, #MASK_CARRY
		ORRCS	rstatus, rstatus, #MASK_CARRY
		UPDATE_ZN
.endm
.macro		A_DEC8		
		@MOV		rscratch,#0		
		SUBS		reg_a, reg_a, #0x01000000
		@STR		rscratch,[reg_cpu_var,#WaitAddress_ofs]
		bic		rstatus, rstatus, #MASK_SHUTDOWN
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		A_DEC16		
		@MOV		rscratch,#0
		SUBS 		reg_a, reg_a, #0x00010000
		@STR		rscratch,[reg_cpu_var,#WaitAddress_ofs]
		bic		rstatus, rstatus, #MASK_SHUTDOWN		
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		DEC16		
		S9xGetWordRegNS rscratch2	       @  do not	destroy	Opadress in rscratch		
		@MOV		rscratch3,#0
		SUBS		rscratch2, rscratch2, #0x00010000
		@STR		rscratch3,[reg_cpu_var,#WaitAddress_ofs]
		bic		rstatus, rstatus, #MASK_SHUTDOWN
		UPDATE_ZN		
		S9xSetWord 	rscratch2
		ADD1CYCLE
.endm
.macro		DEC8
		S9xGetByteRegNS rscratch2	       @  do not	destroy	Opadress in rscratch
		@MOV		rscratch3,#0
		SUBS		rscratch2, rscratch2, #0x01000000
		@STR		rscratch3,[reg_cpu_var,#WaitAddress_ofs]
		bic		rstatus, rstatus, #MASK_SHUTDOWN
		UPDATE_ZN		
		S9xSetByte 	rscratch2
		ADD1CYCLE
.endm
.macro		EOR16
		S9xGetWord
		EORS		reg_a, reg_a, rscratch
		UPDATE_ZN
.endm
.macro		EOR8
		S9xGetByte
		EORS		reg_a, reg_a, rscratch
		UPDATE_ZN
.endm
.macro		A_INC8		
		@MOV		rscratch3,#0
		ADDS		reg_a, reg_a, #0x01000000
		@STR		rscratch3,[reg_cpu_var,#WaitAddress_ofs]
		bic		rstatus, rstatus, #MASK_SHUTDOWN
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		A_INC16		
		@MOV		rscratch3,#0	
		ADDS		reg_a, reg_a, #0x00010000
		@STR		rscratch3,[reg_cpu_var,#WaitAddress_ofs]
		bic		rstatus, rstatus, #MASK_SHUTDOWN
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		INC16		
		S9xGetWordRegNS	rscratch2
		@MOV		rscratch3,#0
		ADDS		rscratch2, rscratch2, #0x00010000
		@STR		rscratch3,[reg_cpu_var,#WaitAddress_ofs]
		bic		rstatus, rstatus, #MASK_SHUTDOWN
		UPDATE_ZN		
		S9xSetWord 	rscratch2
		ADD1CYCLE
.endm
.macro		INC8		
		S9xGetByteRegNS	rscratch2
		@MOV		rscratch3,#0
		ADDS		rscratch2, rscratch2, #0x01000000
		@STR		rscratch3,[reg_cpu_var,#WaitAddress_ofs]
		bic		rstatus, rstatus, #MASK_SHUTDOWN
		UPDATE_ZN		
		S9xSetByte 	rscratch2
		ADD1CYCLE
.endm
.macro		LDA16
		S9xGetWordRegStatus reg_a
		UPDATE_ZN
.endm
.macro		LDA8
		S9xGetByteRegStatus reg_a
		UPDATE_ZN
.endm
.macro		LDX16
		S9xGetWordRegStatus reg_x
		UPDATE_ZN
.endm
.macro		LDX8
		S9xGetByteRegStatus reg_x
		UPDATE_ZN
.endm
.macro		LDY16
		S9xGetWordRegStatus reg_y
		UPDATE_ZN
.endm
.macro		LDY8
		S9xGetByteRegStatus reg_y
		UPDATE_ZN
.endm
.macro		A_LSR16				
		BIC	rstatus, rstatus, #MASK_NEG	 @  0 : AND mask	11111011111 : set N to zero
		MOVS	reg_a, reg_a, LSR #17		 @  hhhhhhhh llllllll 00000000 00000000 -> 00000000 00000000 0hhhhhhh hlllllll
		@  Update Zero
		BICNE	rstatus, rstatus, #MASK_ZERO	 @  0 : AND mask	11111011111 : set Z to zero
		MOV	reg_a, reg_a, LSL #16			@  -> 0lllllll 00000000 00000000	00000000
		ORREQ	rstatus, rstatus, #MASK_ZERO	 @  1 : OR  mask	00000100000  : set Z to	one
		@  Note : the two MOV are included between instruction, to optimize
		@  the pipeline.
		UPDATE_C
		ADD1CYCLE
.endm
.macro		A_LSR8		
		BIC	rstatus, rstatus, #MASK_NEG	 @  0 : AND mask	11111011111 : set N to zero
		MOVS	reg_a, reg_a, LSR #25		 @  llllllll 00000000 00000000 00000000 -> 00000000 00000000 00000000 0lllllll
		@  Update Zero
		BICNE	rstatus, rstatus, #MASK_ZERO	 @  0 : AND mask	11111011111 : set Z to zero
		MOV	reg_a, reg_a, LSL #24			@  -> 00000000 00000000 00000000	0lllllll
		ORREQ	rstatus, rstatus, #MASK_ZERO	 @  1 : OR  mask	00000100000  : set Z to	one		
		@  Note : the two MOV are included between instruction, to optimize
		@  the pipeline.
		UPDATE_C
		ADD1CYCLE
.endm
.macro		LSR16				
		S9xGetWordRegNS	rscratch2
		@  N set to zero by >> 1 LSR
		BIC		rstatus, rstatus, #MASK_NEG	 @  0 : AND mask	11111011111 : set N to zero
		MOVS		rscratch2, rscratch2, LSR #17		   @  llllllll 00000000 00000000	00000000 -> 00000000 00000000 00000000 0lllllll
		@  Update Carry		
		BICCC		rstatus, rstatus, #MASK_CARRY  @ 	0 : AND	mask 11111011111 : set C to zero		
		ORRCS		rstatus, rstatus, #MASK_CARRY      @ 	1 : OR	mask 00000100000 : set C to one
		@  Update Zero
		BICNE		rstatus, rstatus, #MASK_ZERO	 @  0 : AND mask	11111011111 : set Z to zero
		ORREQ		rstatus, rstatus, #MASK_ZERO	 @  1 : OR  mask	00000100000  : set Z to	one	
		S9xSetWordLow 	rscratch2
		ADD1CYCLE
.endm
.macro		LSR8				
		S9xGetByteRegNS	rscratch2
		@  N set to zero by >> 1 LSR
		BIC		rstatus, rstatus, #MASK_NEG	 @  0 : AND mask	11111011111 : set N to zero
		MOVS		rscratch2, rscratch2, LSR #25		   @  llllllll 00000000 00000000	00000000 -> 00000000 00000000 00000000 0lllllll
		@  Update Carry		
		BICCC		rstatus, rstatus, #MASK_CARRY  @ 	0 : AND	mask 11111011111 : set C to zero		
		ORRCS		rstatus, rstatus, #MASK_CARRY      @ 	1 : OR	mask 00000100000 : set C to one
		@  Update Zero
		BICNE		rstatus, rstatus, #MASK_ZERO	 @  0 : AND mask	11111011111 : set Z to zero
		ORREQ		rstatus, rstatus, #MASK_ZERO	 @  1 : OR  mask	00000100000  : set Z to	one		
		S9xSetByteLow 	rscratch2
		ADD1CYCLE
.endm
.macro		ORA8
		S9xGetByte
		ORRS		reg_a, reg_a, rscratch
		UPDATE_ZN
.endm
.macro		ORA16
		S9xGetWord
		ORRS		reg_a, reg_a, rscratch
		UPDATE_ZN
.endm
.macro		A_ROL16		
		TST		rstatus, #MASK_CARRY
		ORRNE		reg_a, reg_a, #0x00008000
		MOVS		reg_a, reg_a, LSL #1
		UPDATE_ZN
		UPDATE_C
		ADD1CYCLE
.endm
.macro		A_ROL8		
		TST		rstatus, #MASK_CARRY
		ORRNE		reg_a, reg_a, #0x00800000
		MOVS		reg_a, reg_a, LSL #1
		UPDATE_ZN
		UPDATE_C
		ADD1CYCLE
.endm
.macro		ROL16		
		S9xGetWordRegNS	rscratch2
		TST		rstatus, #MASK_CARRY
		ORRNE		rscratch2, rscratch2, #0x00008000
		MOVS		rscratch2, rscratch2, LSL #1
		UPDATE_ZN
		UPDATE_C		
		S9xSetWord 	rscratch2
		ADD1CYCLE
.endm
.macro		ROL8		
		S9xGetByteRegNS	rscratch2
		TST		rstatus, #MASK_CARRY
		ORRNE		rscratch2, rscratch2, #0x00800000
		MOVS		rscratch2, rscratch2, LSL #1
		UPDATE_ZN
		UPDATE_C		
		S9xSetByte 	rscratch2
		ADD1CYCLE
.endm
.macro		A_ROR16		
		MOV			reg_a,reg_a, LSR #16
		TST			rstatus, #MASK_CARRY
		ORRNE			reg_a, reg_a, #0x00010000
		ORRNE			rstatus,rstatus,#MASK_NEG
		BICEQ			rstatus,rstatus,#MASK_NEG		
		MOVS			reg_a,reg_a,LSR #1
		UPDATE_C
		UPDATE_Z		
		MOV			reg_a,reg_a, LSL #16
		ADD1CYCLE
.endm
.macro		A_ROR8				
		MOV			reg_a,reg_a, LSR #24
		TST			rstatus, #MASK_CARRY
		ORRNE			reg_a, reg_a, #0x00000100
		ORRNE			rstatus,rstatus,#MASK_NEG
		BICEQ			rstatus,rstatus,#MASK_NEG		
		MOVS			reg_a,reg_a,LSR #1
		UPDATE_C
		UPDATE_Z		
		MOV			reg_a,reg_a, LSL #24
		ADD1CYCLE
.endm
.macro		ROR16		
		S9xGetWordLowRegNS	rscratch2
		TST			rstatus, #MASK_CARRY
		ORRNE			rscratch2, rscratch2, #0x00010000
		ORRNE			rstatus,rstatus,#MASK_NEG
		BICEQ			rstatus,rstatus,#MASK_NEG		
		MOVS			rscratch2,rscratch2,LSR #1
		UPDATE_C
		UPDATE_Z
		S9xSetWordLow 	rscratch2
		ADD1CYCLE

.endm
.macro		ROR8		
		S9xGetByteLowRegNS	rscratch2
		TST			rstatus, #MASK_CARRY
		ORRNE			rscratch2, rscratch2, #0x00000100
		ORRNE			rstatus,rstatus,#MASK_NEG
		BICEQ			rstatus,rstatus,#MASK_NEG		
		MOVS			rscratch2,rscratch2,LSR #1
		UPDATE_C
		UPDATE_Z
		S9xSetByteLow 	rscratch2
		ADD1CYCLE
.endm

.macro SBC16
        TST rstatus, #MASK_DECIMAL
		BEQ 1111f
		@ decimal mode: the C core's arithmetic (os9x_asm_cpu.cpp)
		S9xGetWordLow
		MOV	r1, r0
		MOV	r0, reg_a, LSR #16
		TST	rstatus, #MASK_CARRY
		MOVEQ	r2, #0
		MOVNE	r2, #1
		CCALL	asm_DecimalSBC16
		TST	r0, #0x10000
		ORRNE	rstatus, rstatus, #MASK_CARRY
		BICEQ	rstatus, rstatus, #MASK_CARRY
		TST	r0, #0x20000
		ORRNE	rstatus, rstatus, #MASK_OVERFLOW
		BICEQ	rstatus, rstatus, #MASK_OVERFLOW
		MOVS	reg_a, r0, LSL #16
		UPDATE_ZN
		B	1113f
1111:
		S9xGetWordLow 
		MOVS rscratch2,rstatus,LSR #MASK_SHIFTER_CARRY
		SBCS reg_a, reg_a, rscratch, LSL #16 
		@ OverFlow 
		ORRVS rstatus, rstatus, #MASK_OVERFLOW
		BICVC rstatus, rstatus, #MASK_OVERFLOW
		MOV reg_a, reg_a, LSR #16
		@ Carry
		UPDATE_C
		MOVS reg_a, reg_a, LSL #16
		@ Update flag
		UPDATE_ZN
1113:
.endm 

.macro SBC8
		TST rstatus, #MASK_DECIMAL 
		BEQ 1111f		
		@ decimal mode: the C core's arithmetic (os9x_asm_cpu.cpp)
		S9xGetByteLow
		MOV	r1, r0
		MOV	r0, reg_a, LSR #24
		TST	rstatus, #MASK_CARRY
		MOVEQ	r2, #0
		MOVNE	r2, #1
		CCALL	asm_DecimalSBC8
		TST	r0, #0x100
		ORRNE	rstatus, rstatus, #MASK_CARRY
		BICEQ	rstatus, rstatus, #MASK_CARRY
		TST	r0, #0x200
		ORRNE	rstatus, rstatus, #MASK_OVERFLOW
		BICEQ	rstatus, rstatus, #MASK_OVERFLOW
		MOVS	reg_a, r0, LSL #24
		UPDATE_ZN
		B	1113f
1111:
		S9xGetByteLow
		MOVS rscratch2,rstatus,LSR #MASK_SHIFTER_CARRY
		SBCS reg_a, reg_a, rscratch, LSL #24 
		@ OverFlow 
		ORRVS rstatus, rstatus, #MASK_OVERFLOW
		BICVC rstatus, rstatus, #MASK_OVERFLOW 
		@ Carry
		UPDATE_C 
		@ Update flag
		ANDS reg_a, reg_a, #0xFF000000
		UPDATE_ZN
1113:
.endm 

.macro		STA16
		S9xSetWord	reg_a
.endm
.macro		STA8
		S9xSetByte	reg_a
.endm
.macro		STX16
		S9xSetWord	reg_x
.endm
.macro		STX8
		S9xSetByte	reg_x
.endm
.macro		STY16
		S9xSetWord	reg_y
.endm
.macro		STY8
		S9xSetByte	reg_y
.endm
.macro		STZ16
		S9xSetWordZero
.endm
.macro		STZ8		
		S9xSetByteZero
.endm
.macro		TSB16			
		S9xGetWordRegNS	rscratch2
		TST		reg_a, rscratch2
		BICNE		rstatus, rstatus, #MASK_ZERO	 @  0 : AND mask	11111011111 : set Z to zero
		ORREQ		rstatus, rstatus, #MASK_ZERO	 @  1 : OR  mask	00000100000  : set Z to	one		
		ORR		rscratch2, reg_a, rscratch2		
		S9xSetWord 	rscratch2
		ADD1CYCLE
.endm
.macro		TSB8				
		S9xGetByteRegNS	rscratch2
		TST		reg_a, rscratch2
		BICNE		rstatus, rstatus, #MASK_ZERO	 @  0 : AND mask	11111011111 : set Z to zero
		ORREQ		rstatus, rstatus, #MASK_ZERO	 @  1 : OR  mask	00000100000  : set Z to	one
		ORR		rscratch2, reg_a, rscratch2				
		S9xSetByte 	rscratch2
		ADD1CYCLE
.endm
.macro		TRB16		
		S9xGetWordRegNS	rscratch2
		TST		reg_a, rscratch2
		BICNE		rstatus, rstatus, #MASK_ZERO	 @  0 : AND mask	11111011111 : set Z to zero
		ORREQ		rstatus, rstatus, #MASK_ZERO	 @  1 : OR  mask	00000100000  : set Z to	one
		MVN		rscratch3, reg_a
		AND		rscratch2, rscratch3, rscratch2
		S9xSetWord 	rscratch2
		ADD1CYCLE
.endm
.macro		TRB8				
		S9xGetByteRegNS	rscratch2
		TST		reg_a, rscratch2
		BICNE		rstatus, rstatus, #MASK_ZERO	 @  0 : AND mask	11111011111 : set Z to zero
		ORREQ		rstatus, rstatus, #MASK_ZERO	 @  1 : OR  mask	00000100000  : set Z to	one
		MVN		rscratch3, reg_a
		AND		rscratch2, rscratch3, rscratch2		
		S9xSetByte 	rscratch2
		ADD1CYCLE
.endm
/**************************************************************************/


/**************************************************************************/

.macro		Op09M0		/*ORA*/
		LDRB		rscratch2, [rpc,#1]
		LDRB		rscratch, [rpc], #2
		ORR		rscratch2,rscratch,rscratch2,LSL #8
		ORRS		reg_a,reg_a,rscratch2,LSL #16
		UPDATE_ZN
		ADD2MEM
.endm
.macro		Op09M1		/*ORA*/
		LDRB		rscratch, [rpc], #1
		ORRS		reg_a,reg_a,rscratch,LSL #24
		UPDATE_ZN
		ADD1MEM
.endm
/***********************************************************************/
.macro		Op90 	/*BCC*/
		asmRelative		
		BranchCheck0
		TST		rstatus, #MASK_CARRY
		BNE		1111f
                ADD 		rpc, rscratch, regpcbase @  rpc = OpAddress +PCBase
                ADD1CYCLEX	1
                CPUShutdown
1111:
.endm
.macro		OpB0	/*BCS*/
		asmRelative		
		BranchCheck0
		TST		rstatus, #MASK_CARRY
		BEQ		1111f
                ADD 		rpc, rscratch, regpcbase @  rpc = OpAddress +PCBase
                ADD1CYCLEX	1
                CPUShutdown
1111:
.endm
.macro		OpF0 	/*BEQ*/
		asmRelative		
		BranchCheck2
		TST		rstatus, #MASK_ZERO
		BEQ		1111f
                ADD 		rpc, rscratch, regpcbase @  rpc = OpAddress +PCBase
                ADD1CYCLEX	1
                CPUShutdown
1111:
.endm
.macro		OpD0	/*BNE*/
		asmRelative		
		BranchCheck1
		TST		rstatus, #MASK_ZERO
		BNE		1111f
                ADD 		rpc, rscratch, regpcbase @  rpc = OpAddress +PCBase
                ADD1CYCLEX	1
                CPUShutdown
1111:
.endm
.macro		Op30	/*BMI*/
		asmRelative		
		BranchCheck0
		TST		rstatus, #MASK_NEG
		BEQ		1111f
                ADD 		rpc, rscratch, regpcbase @  rpc = OpAddress +PCBase
                ADD1CYCLEX	1
                CPUShutdown
1111:
.endm
.macro		Op10   /*BPL*/
		asmRelative
		BranchCheck1
		TST 		rstatus, #MASK_NEG @  neg, z!=0, NE
		BNE		1111f
                ADD 		rpc, rscratch, regpcbase @  rpc = OpAddress + PCBase
                ADD1CYCLEX	1
                CPUShutdown
1111:                
.endm
.macro		Op50   /*BVC*/
		asmRelative
		BranchCheck0
		TST 		rstatus, #MASK_OVERFLOW @  neg, z!=0, NE
		BNE		1111f
                ADD 		rpc, rscratch, regpcbase @  rpc = OpAddress + PCBase
                ADD1CYCLEX	1
                CPUShutdown
1111:                
.endm
.macro		Op70   /*BVS*/
		asmRelative
		BranchCheck0
		TST 		rstatus, #MASK_OVERFLOW @  neg, z!=0, NE
		BEQ		1111f
                ADD 		rpc, rscratch, regpcbase @  rpc = OpAddress + PCBase
                ADD1CYCLEX	1
                CPUShutdown
1111:                
.endm
.macro		Op80   /*BRA*/
		asmRelative				
                ADD 		rpc, rscratch, regpcbase @  rpc = OpAddress + PCBase
                ADD1CYCLEX	1
                CPUShutdown
1111:                
.endm
/*******************************************************************************************/
/************************************************************/
/* SetFlag Instructions ********************************************************************** */
.macro		Op38 /*SEC*/		
		ORR		rstatus, rstatus, #MASK_CARRY      @ 	1 : OR	mask 00000100000 : set C to one
		ADD1CYCLE
.endm
.macro		OpF8 /*SED*/		
		SetDecimal
		ADD1CYCLE		
.endm
.macro		Op78 /*SEI*/
		SetIRQ
		ADD1CYCLE
.endm


/****************************************************************************************/
/* ClearFlag Instructions ******************************************************************** */		
.macro		Op18  /*CLC*/		
		BIC 		rstatus, rstatus, #MASK_CARRY
		ADD1CYCLE
.endm
.macro		OpD8 /*CLD*/		
		ClearDecimal
		ADD1CYCLE
.endm
.macro		Op58  /*CLI*/		
		ClearIRQ
		ADD1CYCLE		
		@ CHECK_FOR_IRQ
.endm
.macro		OpB8 /*CLV*/		
		BIC 		rstatus, rstatus, #MASK_OVERFLOW
		ADD1CYCLE     
.endm

/******************************************************************************************/
/* DEX/DEY *********************************************************************************** */

.macro		OpCAX1  /*DEX*/
		@MOV		rscratch3,#0
		SUBS 		reg_x, reg_x, #0x01000000
		@STR		rscratch3,[reg_cpu_var,#WaitAddress_ofs]
		bic		rstatus, rstatus, #MASK_SHUTDOWN
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		OpCAX0  /*DEX*/		
		@MOV		rscratch3,#0
		SUBS 		reg_x, reg_x, #0x00010000
		@STR		rscratch3,[reg_cpu_var,#WaitAddress_ofs]
		bic		rstatus, rstatus, #MASK_SHUTDOWN
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		Op88X1 /*DEY*/
		@MOV		rscratch3,#0
		SUBS 		reg_y, reg_y, #0x01000000
		@STR		rscratch3,[reg_cpu_var,#WaitAddress_ofs]
		bic		rstatus, rstatus, #MASK_SHUTDOWN
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		Op88X0 /*DEY*/
		@MOV		rscratch3,#0
		SUBS 		reg_y, reg_y, #0x00010000
		@STR		rscratch3,[reg_cpu_var,#WaitAddress_ofs]
		bic		rstatus, rstatus, #MASK_SHUTDOWN
		UPDATE_ZN
		ADD1CYCLE
.endm

/******************************************************************************************/
/* INX/INY *********************************************************************************** */		
.macro		OpE8X1
		@MOV		rscratch3,#0
		ADDS 		reg_x, reg_x, #0x01000000
		@STR		rscratch3,[reg_cpu_var,#WaitAddress_ofs]
		bic		rstatus, rstatus, #MASK_SHUTDOWN
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		OpE8X0
		@MOV		rscratch3,#0
		ADDS 		reg_x, reg_x, #0x00010000
		@STR		rscratch3,[reg_cpu_var,#WaitAddress_ofs]
		bic		rstatus, rstatus, #MASK_SHUTDOWN
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		OpC8X1
		@MOV		rscratch3,#0
		ADDS 		reg_y, reg_y, #0x01000000
		@STR		rscratch3,[reg_cpu_var,#WaitAddress_ofs]
		bic		rstatus, rstatus, #MASK_SHUTDOWN
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		OpC8X0		
		@MOV		rscratch3,#0
		ADDS 		reg_y, reg_y, #0x00010000
		@STR		rscratch3,[reg_cpu_var,#WaitAddress_ofs]
		bic		rstatus, rstatus, #MASK_SHUTDOWN		
		UPDATE_ZN
		ADD1CYCLE
.endm

/**********************************************************************************************/

/* NOP *************************************************************************************** */		
.macro		OpEA		
		ADD1CYCLE
.endm

/**************************************************************************/
/* PUSH Instructions **************************************************** */
.macro		OpF4
		Absolute		
		PushWrLow
.endm
.macro		OpD4
		DirectIndirect		
		PushWrLow
.endm
.macro		Op62
		asmRelativeLong
		PushWrLow
.endm
.macro		Op48M0		
		PushW 		reg_a
		ADD1CYCLE
.endm
.macro		Op48M1		
		PushB 		reg_a
		ADD1CYCLE
.endm
.macro		Op8B
		AND		rscratch2, reg_d_bank, #0xFF
		PushBLow 	rscratch2
		ADD1CYCLE
.endm
.macro		Op0B
		PushW	 	reg_d
		ADD1CYCLE
.endm
.macro		Op4B
		PushBlow	reg_p_bank
		ADD1CYCLE
.endm
.macro		Op08		
		PushB	 	rstatus
		ADD1CYCLE
.endm
.macro		OpDAX1
		PushB 		reg_x
		ADD1CYCLE
.endm
.macro		OpDAX0
		PushW 		reg_x
		ADD1CYCLE
.endm
.macro		Op5AX1		
		PushB 		reg_y
		ADD1CYCLE
.endm
.macro		Op5AX0
		PushW 		reg_y
		ADD1CYCLE
.endm
/**************************************************************************/
/* PULL Instructions **************************************************** */
.macro		Op68M1
		PullBS		reg_a
		UPDATE_ZN
		ADD2CYCLE
.endm
.macro		Op68M0
		PullWS		reg_a
		UPDATE_ZN
		ADD2CYCLE
.endm
.macro		OpAB
		BIC		reg_d_bank,reg_d_bank, #0xFF
		PullBrS 	
		ORR		reg_d_bank,reg_d_bank,rscratch, LSR #24
		UPDATE_ZN
		ADD2CYCLE
.endm
.macro		Op2B		
		BIC		reg_d,reg_d, #0xFF000000
		BIC		reg_d,reg_d, #0x00FF0000
		PullWrS		
		ORR		reg_d,rscratch,reg_d
		UPDATE_ZN
		ADD2CYCLE
.endm
.macro		Op28X1M1	/*PLP*/
		@ INDEX set, MEMORY set
		BIC		rstatus,rstatus,#0xFF000000
		PullBr
		ORR		rstatus,rscratch,rstatus
		TST 		rstatus, #MASK_INDEX		
		@ INDEX clear & was set : 8->16
		MOVEQ		reg_x,reg_x,LSR #8
		MOVEQ		reg_y,reg_y,LSR #8		
		TST 		rstatus, #MASK_MEM		
		@ MEMORY cleared & was set : 8->16
		LDREQB		rscratch,[reg_cpu_var,#RAH_ofs]		
		MOVEQ		reg_a,reg_a,LSR #8
		ORREQ		reg_a,reg_a,rscratch, LSL #24
		S9xFixCycles
		ADD2CYCLE
.endm
.macro		Op28X0M1	/*PLP*/		
		@ INDEX cleared, MEMORY set
		BIC		rstatus,rstatus,#0xFF000000				
		PullBr		
		ORR		rstatus,rscratch,rstatus
		TST 		rstatus, #MASK_INDEX
		@ INDEX set & was cleared : 16->8
		MOVNE		reg_x,reg_x,LSL #8
		MOVNE		reg_y,reg_y,LSL #8
		TST 		rstatus, #MASK_MEM
		@ MEMORY cleared & was set : 8->16
		LDREQB		rscratch,[reg_cpu_var,#RAH_ofs]
		MOVEQ		reg_a,reg_a,LSR #8
		ORREQ		reg_a,reg_a,rscratch, LSL #24
		S9xFixCycles
		ADD2CYCLE
.endm
.macro		Op28X1M0	/*PLP*/
		@ INDEX set, MEMORY set		
		BIC		rstatus,rstatus,#0xFF000000				
		PullBr		
		ORR		rstatus,rscratch,rstatus
		TST 		rstatus, #MASK_INDEX
		@ INDEX clear & was set : 8->16
		MOVEQ		reg_x,reg_x,LSR #8
		MOVEQ		reg_y,reg_y,LSR #8		
		TST 		rstatus, #MASK_MEM
		@ MEMORY set & was cleared : 16->8				
		MOVNE		rscratch,reg_a,LSR #24
		MOVNE		reg_a,reg_a,LSL #8
		STRNEB		rscratch,[reg_cpu_var,#RAH_ofs]
		S9xFixCycles
		ADD2CYCLE
.endm
.macro		Op28X0M0	/*PLP*/
		@ INDEX set, MEMORY set
		BIC		rstatus,rstatus,#0xFF000000
		PullBr
		ORR		rstatus,rscratch,rstatus
		TST 		rstatus, #MASK_INDEX
		@ INDEX set & was cleared : 16->8
		MOVNE		reg_x,reg_x,LSL #8
		MOVNE		reg_y,reg_y,LSL #8
		TST 		rstatus, #MASK_MEM
		@ MEMORY set & was cleared : 16->8				
		MOVNE		rscratch,reg_a,LSR #24
		MOVNE		reg_a,reg_a,LSL #8
		STRNEB		rscratch,[reg_cpu_var,#RAH_ofs]
		S9xFixCycles
		ADD2CYCLE
.endm
.macro		OpFAX1
		PullBS 		reg_x
		UPDATE_ZN
		ADD2CYCLE
.endm
.macro		OpFAX0	
		PullWS 		reg_x
		UPDATE_ZN
		ADD2CYCLE
.endm
.macro		Op7AX1
		PullBS		reg_y
		UPDATE_ZN
		ADD2CYCLE
.endm
.macro		Op7AX0		
		PullWS 		reg_y
		UPDATE_ZN
		ADD2CYCLE
.endm		

/**********************************************************************************************/
/* Transfer Instructions ********************************************************************* */
.macro		OpAAX1M1 /*TAX8*/		
		MOVS 		reg_x, reg_a
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		OpAAX0M1 /*TAX16*/		
		LDRB 		reg_x, [reg_cpu_var,#RAH_ofs]
		MOV		reg_x, reg_x,LSL #24
		ORRS		reg_x, reg_x,reg_a, LSR #8		
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		OpAAX1M0 /*TAX8*/		
		MOVS 		reg_x, reg_a, LSL #8
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		OpAAX0M0 /*TAX16*/		
		MOVS 		reg_x, reg_a
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		OpA8X1M1 /*TAY8*/		
		MOVS 		reg_y, reg_a
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		OpA8X0M1 /*TAY16*/
		LDRB 		reg_y, [reg_cpu_var,#RAH_ofs]
		MOV		reg_y, reg_y,LSL #24
		ORRS		reg_y, reg_y,reg_a, LSR #8		
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		OpA8X1M0 /*TAY8*/		
		MOVS 		reg_y, reg_a, LSL #8
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		OpA8X0M0 /*TAY16*/
		MOVS 		reg_y, reg_a
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		Op5BM1		
		LDRB		rscratch, [reg_cpu_var,#RAH_ofs]
		MOV		reg_d,reg_d,LSL #16
		MOV		rscratch,rscratch,LSL #24
		ORRS		rscratch,rscratch,reg_a, LSR #8		
		UPDATE_ZN
		ORR  		reg_d,rscratch,reg_d,LSR #16
		ADD1CYCLE
.endm
.macro		Op5BM0		
		MOV		reg_d,reg_d,LSL #16		
		MOVS		reg_a,reg_a
		UPDATE_ZN
		ORR  		reg_d,reg_a,reg_d,LSR #16
		ADD1CYCLE
.endm
.macro		Op1BM1
		TST 		rstatus, #MASK_EMUL
		MOVNE		reg_s, reg_a, LSR #24
		ORRNE		reg_s, reg_s, #0x100		
		LDREQB		reg_s, [reg_cpu_var,#RAH_ofs]
		ORREQ		reg_s, reg_s, reg_a
		MOVEQ		reg_s, reg_s, ROR #24
		ADD1CYCLE
.endm
.macro		Op1BM0		
		MOV 		reg_s, reg_a, LSR #16
		ADD1CYCLE
.endm
.macro		Op7BM1		
		MOVS 		reg_a, reg_d, ASR #16		
		UPDATE_ZN
		MOV		rscratch,reg_a,LSR #8		
		MOV		reg_a,reg_a, LSL #24
		STRB		rscratch, [reg_cpu_var,#RAH_ofs]
		ADD1CYCLE
.endm
.macro		Op7BM0
		MOVS 		reg_a, reg_d, ASR #16		
		UPDATE_ZN
		MOV		reg_a,reg_a, LSL #16
		ADD1CYCLE
.endm
.macro		Op3BM1
		MOV		rscratch,reg_s, LSR #8
		MOVS		reg_a, reg_s, LSL #16
		STRB		rscratch, [reg_cpu_var,#RAH_ofs]
		UPDATE_ZN
		MOV		reg_a,reg_a, LSL #8
		ADD1CYCLE
.endm
.macro		Op3BM0
		MOVS		reg_a, reg_s, LSL #16
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		OpBAX1
		MOVS 		reg_x, reg_s, LSL #24
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		OpBAX0
		MOVS 		reg_x, reg_s, LSL #16
		UPDATE_ZN
		ADD1CYCLE
.endm		
.macro		Op8AM1X1
		MOVS 		reg_a, reg_x
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		Op8AM1X0
		MOVS 		reg_a, reg_x, LSL #8
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		Op8AM0X1
		MOVS 		reg_a, reg_x, LSR #8
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		Op8AM0X0
		MOVS 		reg_a, reg_x
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		Op9AX1		
		MOV 		reg_s, reg_x, LSR #24

		TST 		rstatus, #MASK_EMUL		
		ORRNE		reg_s, reg_s, #0x100
		ADD1CYCLE
.endm
.macro		Op9AX0		
		MOV 		reg_s, reg_x, LSR #16
		ADD1CYCLE
.endm
.macro		Op9BX1		
		MOVS 		reg_y, reg_x
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		Op9BX0		
		MOVS 		reg_y, reg_x
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		Op98M1X1	
		MOVS 		reg_a, reg_y
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		Op98M1X0
		MOVS 		reg_a, reg_y, LSL #8
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		Op98M0X1
		MOVS 		reg_a, reg_y, LSR #8
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		Op98M0X0
		MOVS 		reg_a, reg_y
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		OpBBX1		
		MOVS 		reg_x, reg_y
		UPDATE_ZN
		ADD1CYCLE
.endm
.macro		OpBBX0
		MOVS 		reg_x, reg_y
		UPDATE_ZN
		ADD1CYCLE
.endm

/**********************************************************************************************/
/* XCE *************************************************************************************** */

.macro		OpFB
    TST		rstatus,#MASK_CARRY
    BEQ		1111f
    @ CARRY is set
    TST		rstatus,#MASK_EMUL    
    BNE		1112f
    @ EMUL is cleared
    BIC		rstatus,rstatus,#(MASK_CARRY)
    TST		rstatus,#MASK_INDEX
    @ X & Y were 16bits before
    MOVEQ	reg_x,reg_x,LSL #8
    MOVEQ	reg_y,reg_y,LSL #8
    TST		rstatus,#MASK_MEM
    @ A was 16bits before
    @ save AH
    MOVEQ	rscratch,reg_a,LSR #24
    STREQB	rscratch,[reg_cpu_var,#RAH_ofs]
    MOVEQ	reg_a,reg_a,LSL #8
    ORR		rstatus,rstatus,#(MASK_EMUL|MASK_MEM|MASK_INDEX)
    AND		reg_s,reg_s,#0xFF
    ORR		reg_s,reg_s,#0x100    
    B		1113f    
1112:    
    @ EMUL is set
    TST		rstatus,#MASK_INDEX
    @ X & Y were 16bits before
    MOVEQ	reg_x,reg_x,LSL #8
    MOVEQ	reg_y,reg_y,LSL #8
    TST		rstatus,#MASK_MEM
    @ A was 16bits before
    @ save AH
    MOVEQ	rscratch,reg_a,LSR #24
    STREQB	rscratch,[reg_cpu_var,#RAH_ofs]
    MOVEQ	reg_a,reg_a,LSL #8
    ORR		rstatus,rstatus,#(MASK_CARRY|MASK_MEM|MASK_INDEX)
    AND		reg_s,reg_s,#0xFF
    ORR		reg_s,reg_s,#0x100    
    B		1113f
1111:    
    @ CARRY is cleared
    TST		rstatus,#MASK_EMUL
    BEQ		1113f		@ native stays native, carry stays clear (psnes set it)
    @ EMUL was set : X,Y & A were 8bits
    @ Now have to check MEMORY & INDEX for potential conversions to 16bits
    TST		rstatus,#MASK_INDEX
    @  X & Y are now 16bits
    MOVEQ	reg_x,reg_x,LSR #8	
    MOVEQ	reg_y,reg_y,LSR #8	
    TST		rstatus,#MASK_MEM
    @  A is now 16bits
    MOVEQ	reg_a,reg_a,LSR #8	
    @ restore AH
    LDREQB	rscratch,[reg_cpu_var,#RAH_ofs]    
    ORREQ	reg_a,reg_a,rscratch,LSL #24
1115:    
    BIC		rstatus,rstatus,#(MASK_EMUL)
    ORR		rstatus,rstatus,#(MASK_CARRY)
1113:
    ADD1CYCLE
    S9xFixCycles
.endm

/*******************************************************************************/
/* BRK *************************************************************************/
.macro		Op00		/*BRK*/
		MOV		rscratch,#1
		STRB		rscratch,[reg_cpu_var,#BRKTriggered_ofs]
		
		TST 		rstatus, #MASK_EMUL
		@  EQ is flag to zero (!CheckEmu)
		BNE 		2001f@ elseOp00
		PushBLow	reg_p_bank
		SUB 		rscratch, rpc, regpcbase
		ADD 		rscratch2, rscratch, #1
		PushWLow 	rscratch2
		@  PackStatus
		PushB	 	rstatus
		MOV		rscratch, rstatus, LSR #24	@ OpenBus = the P pushed, as cpuops.cpp
		STRB		rscratch, [reg_cpu_var, #OpenBus_ofs]
		ClearDecimal
		SetIRQ
		BIC 		reg_p_bank, reg_p_bank, #0xFF
		MOV 		rscratch, #0xE6
		ORR 		rscratch, rscratch, #0xFF00
		S9xGetWordLow 		
		S9xSetPCBase 	
		ADD2CYCLEX	8
		B 		2002f@ endOp00
2001:@ elseOp00
		SUB 		rscratch2, rpc, regpcbase
		PushWLow 	rscratch2
		@  PackStatus
		PushB	 	rstatus
		MOV		rscratch, rstatus, LSR #24	@ OpenBus = the P pushed, as cpuops.cpp
		STRB		rscratch, [reg_cpu_var, #OpenBus_ofs]
		ClearDecimal
		SetIRQ
		BIC		reg_p_bank,reg_p_bank, #0xFF
		MOV 		rscratch, #0xFE
		ORR 		rscratch, rscratch, #0xFF00
		S9xGetWordLow 		
		S9xSetPCBase 	
		ADD1CYCLEX	6
2002:@ endOp00
.endm


/**********************************************************************************************/
/* BRL ************************************************************************************** */
.macro		Op82	/*BRL*/
		asmRelativeLong
		ORR		rscratch, rscratch, reg_p_bank, LSL #16
		S9xSetPCBase
.endm		
/**********************************************************************************************/
/* IRQ *************************************************************************************** */			
@ void S9xOpcode_IRQ (void)		
.macro		S9xOpcode_IRQ	@ IRQ
		TST 		rstatus, #MASK_EMUL
		@  EQ is flag to zero (!CheckEmu)
		BNE 		2121f@ elseOp02
		PushBLow 	reg_p_bank
		SUB 		rscratch2, rpc, regpcbase
		PushWLow 	rscratch2
		@  PackStatus
		PushB	 	rstatus
		MOV		rscratch, rstatus, LSR #24	@ OpenBus = the P pushed, as cpuops.cpp
		STRB		rscratch, [reg_cpu_var, #OpenBus_ofs]
		ClearDecimal
		SetIRQ
		BIC 		reg_p_bank, reg_p_bank,#0xFF
		MOV 		rscratch, #0xEE
		ORR 		rscratch, rscratch, #0xFF00
		S9xGetWordLow 		
		S9xSetPCBase 	
		@ADD2CYCLE
		ADD2CYCLEX	8
		B 2122f
2121:@ else
		SUB 		rscratch2, rpc, regpcbase
		PushWLow 	rscratch2
		@  PackStatus
		PushB	 	rstatus
		MOV		rscratch, rstatus, LSR #24	@ OpenBus = the P pushed, as cpuops.cpp
		STRB		rscratch, [reg_cpu_var, #OpenBus_ofs]
		ClearDecimal
		SetIRQ
		BIC 		reg_p_bank,reg_p_bank, #0xFF
		MOV 		rscratch, #0xFE
		ORR 		rscratch, rscratch, #0xFF00
		S9xGetWordLow 		
		S9xSetPCBase 	
		@ADD1CYCLE
		ADD1CYCLEX	6
2122:
.endm

/*
void asm_S9xOpcode_IRQ(void)
{
    if (!CheckEmulation())
    {
        PushB (Registers.PB);
        PushW (CPU.PC - CPU.PCBase);
        PushB (Registers.PL);
        ClearDecimal ();
        SetIRQ ();

        Registers.PB = 0;
		S9xSetPCBase (S9xGetWord (0xFFEE));
        CPU.Cycles += TWO_CYCLES;
    }
    else
    {
        PushW (CPU.PC - CPU.PCBase);
        PushB (Registers.PL);
        ClearDecimal ();
        SetIRQ ();

        Registers.PB = 0;
        S9xSetPCBase (S9xGetWord (0xFFFE));
        CPU.Cycles += ONE_CYCLE;
    }
}
*/	
		
/**********************************************************************************************/
/* NMI *************************************************************************************** */		
@ void S9xOpcode_NMI (void)
.macro		S9xOpcode_NMI	@ NMI
		TST 		rstatus, #MASK_EMUL
		@  EQ is flag to zero (!CheckEmu)
		BNE 		2123f@ elseOp02
		PushBLow 	reg_p_bank
		SUB 		rscratch2, rpc, regpcbase
		PushWLow 	rscratch2
		@  PackStatus
		PushB	 	rstatus
		MOV		rscratch, rstatus, LSR #24	@ OpenBus = the P pushed, as cpuops.cpp
		STRB		rscratch, [reg_cpu_var, #OpenBus_ofs]
		ClearDecimal
		SetIRQ
		BIC 		reg_p_bank, reg_p_bank,#0xFF
		MOV 		rscratch, #0xEA
		ORR 		rscratch, rscratch, #0xFF00
		S9xGetWordLow 		
		S9xSetPCBase 	
		@ADD2CYCLE
		ADD2CYCLEX	8
		B 2124f
2123:@ else
		SUB 		rscratch2, rpc, regpcbase
		PushWLow 	rscratch2
		@  PackStatus
		PushB	 	rstatus
		MOV		rscratch, rstatus, LSR #24	@ OpenBus = the P pushed, as cpuops.cpp
		STRB		rscratch, [reg_cpu_var, #OpenBus_ofs]
		ClearDecimal
		SetIRQ
		BIC 		reg_p_bank,reg_p_bank, #0xFF
		MOV 		rscratch, #0xFA
		ORR 		rscratch, rscratch, #0xFF00
		S9xGetWordLow 		
		S9xSetPCBase 	
		@ADD1CYCLE
		ADD1CYCLEX	6
2124:
.endm
/*
void asm_S9xOpcode_NMI(void)
{	
	if (!CheckEmulation())
    {
        PushB (Registers.PB);
        PushW (CPU.PC - CPU.PCBase);
        PushB (Registers.PL);
        ClearDecimal ();
        SetIRQ ();

        Registers.PB = 0;
        S9xSetPCBase (S9xGetWord (0xFFEA));
        CPU.Cycles += TWO_CYCLES;
    }
    else
    {
        PushW (CPU.PC - CPU.PCBase);
        PushB (Registers.PL);
        ClearDecimal ();
        SetIRQ ();

        Registers.PB = 0;
        S9xSetPCBase (S9xGetWord (0xFFFA));
        CPU.Cycles += ONE_CYCLE;
    }    
}
*/

/**********************************************************************************************/
/* COP *************************************************************************************** */
.macro		Op02		/*COP*/
		TST 		rstatus, #MASK_EMUL
		@  EQ is flag to zero (!CheckEmu)
		BNE 		2021f@ elseOp02
		PushBLow 	reg_p_bank
		SUB 		rscratch, rpc, regpcbase
		ADD 		rscratch2, rscratch, #1
		PushWLow 	rscratch2
		@  PackStatus
		PushB	 	rstatus
		MOV		rscratch, rstatus, LSR #24	@ OpenBus = the P pushed, as cpuops.cpp
		STRB		rscratch, [reg_cpu_var, #OpenBus_ofs]
		ClearDecimal
		SetIRQ
		BIC 		reg_p_bank, reg_p_bank,#0xFF
		MOV 		rscratch, #0xE4
		ORR 		rscratch, rscratch, #0xFF00
		S9xGetWordLow 		
		S9xSetPCBase 	
		ADD2CYCLEX	8
		B 2022f@ endOp02
2021:@ elseOp02
		SUB 		rscratch2, rpc, regpcbase
		PushWLow 	rscratch2
		@  PackStatus
		PushB	 	rstatus
		MOV		rscratch, rstatus, LSR #24	@ OpenBus = the P pushed, as cpuops.cpp
		STRB		rscratch, [reg_cpu_var, #OpenBus_ofs]
		ClearDecimal
		SetIRQ
		BIC 		reg_p_bank,reg_p_bank, #0xFF
		MOV 		rscratch, #0xF4
		ORR 		rscratch, rscratch, #0xFF00
		S9xGetWordLow 		
		S9xSetPCBase 	
		ADD1CYCLEX	6
2022:@ endOp02
.endm

/**********************************************************************************************/
/* JML *************************************************************************************** */
.macro		OpDC		
		AbsoluteIndirectLong		
		BIC		reg_p_bank,reg_p_bank,#0xFF
		ORR 		reg_p_bank,reg_p_bank, rscratch, LSR #16
		S9xSetPCBase 	
		ADD2CYCLE
.endm
.macro		Op5C		
		AbsoluteLong		
		BIC		reg_p_bank,reg_p_bank,#0xFF
		ORR 		reg_p_bank,reg_p_bank, rscratch, LSR #16
		S9xSetPCBase 	
.endm

/**********************************************************************************************/
/* JMP *************************************************************************************** */
.macro		Op4C
		Absolute
		BIC		rscratch, rscratch, #0xFF0000
		ORR		rscratch, rscratch, reg_p_bank, LSL #16
		S9xSetPCBase
		CPUShutdown
.endm		
.macro		Op6C
		AbsoluteIndirect
		BIC		rscratch, rscratch, #0xFF0000
		ORR		rscratch, rscratch, reg_p_bank, LSL #16
		S9xSetPCBase		
.endm		
.macro		Op7C						
		ADD 		rscratch, rscratch, reg_p_bank, LSL #16
		S9xSetPCBase 	
		ADD1CYCLE
.endm

/**********************************************************************************************/
/* JSL/RTL *********************************************************************************** */
.macro		Op22				
		PushBlow	reg_p_bank
		SUB 		rscratch, rpc, regpcbase
		@ SUB 		rscratch2, rscratch2, #1
		ADD 		rscratch2, rscratch, #2
		PushWlow	rscratch2
		AbsoluteLong		
		BIC		reg_p_bank,reg_p_bank,#0xFF
		ORR 		reg_p_bank, reg_p_bank, rscratch, LSR #16
		S9xSetPCBase 	
.endm
.macro		Op6B		
		PullWLow 	rpc		
		BIC		reg_p_bank,reg_p_bank,#0xFF
		PullBrLow 			
		ORR 		reg_p_bank, reg_p_bank, rscratch
		ADD 		rscratch, rpc, #1
		BIC		rscratch, rscratch,#0xFF0000
		ORR		rscratch, rscratch, reg_p_bank, LSL #16
		S9xSetPCBase
		ADD2CYCLE
.endm
/**********************************************************************************************/
/* JSR/RTS *********************************************************************************** */
.macro		Op20				
		SUB 		rscratch, rpc, regpcbase
		@ SUB 		rscratch2, rscratch2, #1
		ADD 		rscratch2, rscratch, #1		
		PushWlow	rscratch2				
		Absolute		
		BIC		rscratch, rscratch, #0xFF0000		
		ORR 		rscratch, rscratch, reg_p_bank, LSL #16
		S9xSetPCBase 
		ADD1CYCLE
.endm
.macro		OpFCX0
		@ as cpuops.cpp: the address is read before the return address is
		@ pushed (which matters to the idle loop detection)
		AbsoluteIndexedIndirectX0
		STMFD		R13!,{rscratch}
		SUB 		rscratch2, rpc, regpcbase
		SUB		rscratch2, rscratch2, #1
		PushWlow	rscratch2
		LDMFD		R13!,{rscratch}
		ORR 		rscratch, rscratch, reg_p_bank, LSL #16
		S9xSetPCBase
		ADD1CYCLE
.endm
.macro		OpFCX1
		@ as cpuops.cpp: the address is read before the return address is
		@ pushed (which matters to the idle loop detection)
		AbsoluteIndexedIndirectX1
		STMFD		R13!,{rscratch}
		SUB 		rscratch2, rpc, regpcbase
		SUB		rscratch2, rscratch2, #1
		PushWlow	rscratch2
		LDMFD		R13!,{rscratch}
		ORR 		rscratch, rscratch, reg_p_bank, LSL #16
		S9xSetPCBase
		ADD1CYCLE
.endm
.macro		Op60			
		PullWLow 	rpc
		ADD 		rscratch, rpc, #1		
		BIC		rscratch, rscratch,#0x10000		
		ORR		rscratch, rscratch, reg_p_bank, LSL #16		
		S9xSetPCBase 
		ADD3CYCLE
.endm

/**********************************************************************************************/
/* MVN/MVP *********************************************************************************** */		
.macro		Op54X1M1
		ADD2CYCLE2MEM		@ before the moves, as cpuops.cpp
		@ Save RegStatus = reg_d_bank >> 24
		MOV		rscratch, reg_d_bank, LSR #16
                LDRB		reg_d_bank    , [rpc], #1
		LDRB		rscratch2    , [rpc], #1
		STRB		rscratch2, [reg_cpu_var, #OpenBus_ofs]	@ OpenBus = the source bank
		@ Restore RegStatus = reg_d_bank >> 24
		ORR		reg_d_bank, reg_d_bank, rscratch, LSL #16
		MOV		rscratch    , reg_x, LSR #24		
                ORR		rscratch    , rscratch, rscratch2, LSL #16                
		S9xGetByteLow 
		MOV		rscratch2, rscratch
		MOV		rscratch   , reg_y, LSR #24
		ORR		rscratch   , rscratch, reg_d_bank, LSL #16		
		S9xSetByteLow 	rscratch2	
		@ load 16bits A		
		LDRB		rscratch,[reg_cpu_var,#RAH_ofs]
		MOV		reg_a,reg_a,LSR #8
		ORR		reg_a,reg_a,rscratch, LSL #24
		ADD		reg_x, reg_x, #0x01000000
		SUB		reg_a, reg_a, #0x00010000
		ADD		reg_y, reg_y, #0x01000000				
                CMP		reg_a, #0xFFFF0000
                SUBNE		rpc, rpc, #3
                @ update AH
                MOV		rscratch, reg_a, LSR #24
                MOV		reg_a,reg_a,LSL #8
                STRB		rscratch,[reg_cpu_var,#RAH_ofs]                
.endm
.macro		Op54X1M0
		ADD2CYCLE2MEM		@ before the moves, as cpuops.cpp
		@ Save RegStatus = reg_d_bank >> 24
		MOV		rscratch, reg_d_bank, LSR #16
                LDRB		reg_d_bank    , [rpc], #1
		LDRB		rscratch2    , [rpc], #1
		STRB		rscratch2, [reg_cpu_var, #OpenBus_ofs]	@ OpenBus = the source bank
		@ Restore RegStatus = reg_d_bank >> 24
		ORR		reg_d_bank, reg_d_bank, rscratch, LSL #16
		MOV		rscratch    , reg_x, LSR #24		
                ORR		rscratch    , rscratch, rscratch2, LSL #16                
		S9xGetByteLow 
		MOV		rscratch2, rscratch
		MOV		rscratch   , reg_y, LSR #24
		ORR		rscratch   , rscratch, reg_d_bank, LSL #16		
		S9xSetByteLow 	rscratch2		
		ADD		reg_x, reg_x, #0x01000000
		SUB		reg_a, reg_a, #0x00010000
		ADD		reg_y, reg_y, #0x01000000				
                CMP		reg_a, #0xFFFF0000
                SUBNE		rpc, rpc, #3
.endm
.macro		Op54X0M1
		ADD2CYCLE2MEM		@ before the moves, as cpuops.cpp
		@ Save RegStatus = reg_d_bank >> 24
		MOV		rscratch, reg_d_bank, LSR #16
                LDRB		reg_d_bank    , [rpc], #1
		LDRB		rscratch2    , [rpc], #1
		STRB		rscratch2, [reg_cpu_var, #OpenBus_ofs]	@ OpenBus = the source bank
		@ Restore RegStatus = reg_d_bank >> 24
		ORR		reg_d_bank, reg_d_bank, rscratch, LSL #16
		MOV		rscratch    , reg_x, LSR #16
                ORR		rscratch    , rscratch, rscratch2, LSL #16                
		S9xGetByteLow 
		MOV		rscratch2, rscratch
		MOV		rscratch   , reg_y, LSR #16
		ORR		rscratch   , rscratch, reg_d_bank, LSL #16		
		S9xSetByteLow 	rscratch2		
		@ load 16bits A		
		LDRB		rscratch,[reg_cpu_var,#RAH_ofs]
		MOV		reg_a,reg_a,LSR #8
		ORR		reg_a,reg_a,rscratch, LSL #24
		ADD		reg_x, reg_x, #0x00010000
		SUB		reg_a, reg_a, #0x00010000
		ADD		reg_y, reg_y, #0x00010000				
                CMP		reg_a, #0xFFFF0000
                SUBNE		rpc, rpc, #3                
                @ update AH
                MOV		rscratch, reg_a, LSR #24
                MOV		reg_a,reg_a,LSL #8
                STRB		rscratch,[reg_cpu_var,#RAH_ofs]                
.endm
.macro		Op54X0M0
		ADD2CYCLE2MEM		@ before the moves, as cpuops.cpp
		@ Save RegStatus = reg_d_bank >> 24
		MOV		rscratch, reg_d_bank, LSR #16
                LDRB		reg_d_bank    , [rpc], #1
		LDRB		rscratch2    , [rpc], #1
		STRB		rscratch2, [reg_cpu_var, #OpenBus_ofs]	@ OpenBus = the source bank
		@ Restore RegStatus = reg_d_bank >> 24
		ORR		reg_d_bank, reg_d_bank, rscratch, LSL #16
		MOV		rscratch    , reg_x, LSR #16
                ORR		rscratch    , rscratch, rscratch2, LSL #16                
		S9xGetByteLow 
		MOV		rscratch2, rscratch
		MOV		rscratch   , reg_y, LSR #16
		ORR		rscratch   , rscratch, reg_d_bank, LSL #16		
		S9xSetByteLow 	rscratch2		
		ADD		reg_x, reg_x, #0x00010000
		SUB		reg_a, reg_a, #0x00010000
		ADD		reg_y, reg_y, #0x00010000				
                CMP		reg_a, #0xFFFF0000
                SUBNE		rpc, rpc, #3
.endm

.macro		Op44X1M1
		ADD2CYCLE2MEM		@ before the moves, as cpuops.cpp
		@ Save RegStatus = reg_d_bank >> 24
		MOV		rscratch, reg_d_bank, LSR #16
                LDRB		reg_d_bank    , [rpc], #1
		LDRB		rscratch2    , [rpc], #1
		STRB		rscratch2, [reg_cpu_var, #OpenBus_ofs]	@ OpenBus = the source bank
		@ Restore RegStatus = reg_d_bank >> 24
		ORR		reg_d_bank, reg_d_bank, rscratch, LSL #16
		MOV		rscratch    , reg_x, LSR #24		
                ORR		rscratch    , rscratch, rscratch2, LSL #16                
		S9xGetByteLow 
		MOV		rscratch2, rscratch
		MOV		rscratch   , reg_y, LSR #24
		ORR		rscratch   , rscratch, reg_d_bank, LSL #16		
		S9xSetByteLow 	rscratch2
		@ load 16bits A		
		LDRB		rscratch,[reg_cpu_var,#RAH_ofs]
		MOV		reg_a,reg_a,LSR #8
		ORR		reg_a,reg_a,rscratch, LSL #24
		SUB		reg_x, reg_x, #0x01000000
		SUB		reg_a, reg_a, #0x00010000
		SUB		reg_y, reg_y, #0x01000000				
                CMP		reg_a, #0xFFFF0000
                SUBNE		rpc, rpc, #3
                @ update AH
                MOV		rscratch, reg_a, LSR #24
                MOV		reg_a,reg_a,LSL #8
                STRB		rscratch,[reg_cpu_var,#RAH_ofs]                
.endm
.macro		Op44X1M0
		ADD2CYCLE2MEM		@ before the moves, as cpuops.cpp
		@ Save RegStatus = reg_d_bank >> 24
		MOV		rscratch, reg_d_bank, LSR #16
                LDRB		reg_d_bank    , [rpc], #1
		LDRB		rscratch2    , [rpc], #1
		STRB		rscratch2, [reg_cpu_var, #OpenBus_ofs]	@ OpenBus = the source bank
		@ Restore RegStatus = reg_d_bank >> 24
		ORR		reg_d_bank, reg_d_bank, rscratch, LSL #16
		MOV		rscratch    , reg_x, LSR #24		
                ORR		rscratch    , rscratch, rscratch2, LSL #16                
		S9xGetByteLow 
		MOV		rscratch2, rscratch
		MOV		rscratch   , reg_y, LSR #24
		ORR		rscratch   , rscratch, reg_d_bank, LSL #16		
		S9xSetByteLow 	rscratch2		
		SUB		reg_x, reg_x, #0x01000000
		SUB		reg_a, reg_a, #0x00010000
		SUB		reg_y, reg_y, #0x01000000				
                CMP		reg_a, #0xFFFF0000
                SUBNE		rpc, rpc, #3
.endm
.macro		Op44X0M1
		ADD2CYCLE2MEM		@ before the moves, as cpuops.cpp
		@ Save RegStatus = reg_d_bank >> 24
		MOV		rscratch, reg_d_bank, LSR #16
                LDRB		reg_d_bank    , [rpc], #1
		LDRB		rscratch2    , [rpc], #1
		STRB		rscratch2, [reg_cpu_var, #OpenBus_ofs]	@ OpenBus = the source bank
		@ Restore RegStatus = reg_d_bank >> 24
		ORR		reg_d_bank, reg_d_bank, rscratch, LSL #16
		MOV		rscratch    , reg_x, LSR #16
                ORR		rscratch    , rscratch, rscratch2, LSL #16                
		S9xGetByteLow 
		MOV		rscratch2, rscratch
		MOV		rscratch   , reg_y, LSR #16
		ORR		rscratch   , rscratch, reg_d_bank, LSL #16		
		S9xSetByteLow 	rscratch2
		@ load 16bits A		
		LDRB		rscratch,[reg_cpu_var,#RAH_ofs]
		MOV		reg_a,reg_a,LSR #8
		ORR		reg_a,reg_a,rscratch, LSL #24
		SUB		reg_x, reg_x, #0x00010000
		SUB		reg_a, reg_a, #0x00010000
		SUB		reg_y, reg_y, #0x00010000				
                CMP		reg_a, #0xFFFF0000
                SUBNE		rpc, rpc, #3
                @ update AH
                MOV		rscratch, reg_a, LSR #24
                MOV		reg_a,reg_a,LSL #8
                STRB		rscratch,[reg_cpu_var,#RAH_ofs]                
.endm
.macro		Op44X0M0
		ADD2CYCLE2MEM		@ before the moves, as cpuops.cpp
		@ Save RegStatus = reg_d_bank >> 24
		MOV		rscratch, reg_d_bank, LSR #16
                LDRB		reg_d_bank    , [rpc], #1
		LDRB		rscratch2    , [rpc], #1
		STRB		rscratch2, [reg_cpu_var, #OpenBus_ofs]	@ OpenBus = the source bank
		@ Restore RegStatus = reg_d_bank >> 24
		ORR		reg_d_bank, reg_d_bank, rscratch, LSL #16
		MOV		rscratch    , reg_x, LSR #16
                ORR		rscratch    , rscratch, rscratch2, LSL #16                
		S9xGetByteLow 
		MOV		rscratch2, rscratch
		MOV		rscratch   , reg_y, LSR #16
		ORR		rscratch   , rscratch, reg_d_bank, LSL #16		
		S9xSetByteLow 	rscratch2		
		SUB		reg_x, reg_x, #0x00010000
		SUB		reg_a, reg_a, #0x00010000
		SUB		reg_y, reg_y, #0x00010000				
                CMP		reg_a, #0xFFFF0000
                SUBNE		rpc, rpc, #3
.endm

/**********************************************************************************************/
/* REP/SEP *********************************************************************************** */
.macro		OpC2
		@  status&=~(*rpc++);
		@  so possible changes are :		
		@  INDEX = 1 -> 0  : X,Y 8bits -> 16bits
		@  MEM = 1 -> 0 : A 8bits -> 16bits
		@ SAVE OLD status for MASK_INDEX & MASK_MEM comparison
		MOV		rscratch3, rstatus
		LDRB		rscratch, [rpc], #1
		MVN		rscratch, rscratch		
		AND		rstatus,rstatus,rscratch, ROR #(32-STATUS_SHIFTER)
		TST		rstatus,#MASK_EMUL
		BEQ		1111f
		@ emulation mode on : no changes since it was on before opcode
		@ just be sure to reset MEM & INDEX accordingly
		ORR		rstatus,rstatus,#(MASK_MEM|MASK_INDEX)		
		B		1112f
1111:		
		@ NOT in Emulation mode, check INDEX & MEMORY bits
		@ Now check INDEX
		TST		rscratch3,#MASK_INDEX
		BEQ		1113f		
		@  X & Y were 8bit before
		TST		rstatus,#MASK_INDEX
		BNE		1113f
		@  X & Y are now 16bits
		MOV		reg_x,reg_x,LSR #8
		MOV		reg_y,reg_y,LSR #8
1113:		@ X & Y still in 16bits
		@ Now check MEMORY
		TST		rscratch3,#MASK_MEM
		BEQ		1112f		
		@  A was 8bit before
		TST		rstatus,#MASK_MEM
		BNE		1112f
		@  A is now 16bits
		MOV		reg_a,reg_a,LSR #8		
		@ restore AH
    		LDREQB		rscratch,[reg_cpu_var,#RAH_ofs]    		
    		ORREQ		reg_a,reg_a,rscratch,LSL #24
1112:
		S9xFixCycles
		ADD1CYCLE1MEM
.endm
.macro		OpE2
		@  status|=*rpc++;
		@  so possible changes are :
		@  INDEX = 0 -> 1  : X,Y 16bits -> 8bits
		@  MEM = 0 -> 1 : A 16bits -> 8bits
		@ SAVE OLD status for MASK_INDEX & MASK_MEM comparison
		MOV		rscratch3, rstatus
		LDRB		rscratch, [rpc], #1		
		ORR		rstatus,rstatus,rscratch, LSL #STATUS_SHIFTER
		TST		rstatus,#MASK_EMUL
		BEQ		10111f
		@ emulation mode on : no changes sinc eit was on before opcode
		@ just be sure to have mem & index set accordingly
		ORR		rstatus,rstatus,#(MASK_MEM|MASK_INDEX)		
		B		10112f
10111:		
		@ NOT in Emulation mode, check INDEX & MEMORY bits
		@ Now check INDEX
		TST		rscratch3,#MASK_INDEX
		BNE		10113f		
		@  X & Y were 16bit before
		TST		rstatus,#MASK_INDEX
		BEQ		10113f
		@  X & Y are now 8bits
		MOV		reg_x,reg_x,LSL #8
		MOV		reg_y,reg_y,LSL #8
10113:		@ X & Y still in 16bits
		@ Now check MEMORY
		TST		rscratch3,#MASK_MEM
		BNE		10112f		
		@  A was 16bit before
		TST		rstatus,#MASK_MEM
		BEQ		10112f
		@  A is now 8bits
		@  save AH
		MOV		rscratch,reg_a,LSR #24
		MOV		reg_a,reg_a,LSL #8	
		STRB		rscratch,[reg_cpu_var,#RAH_ofs]	
10112:
		S9xFixCycles
		ADD1CYCLE1MEM
.endm

/**********************************************************************************************/
/* XBA *************************************************************************************** */
.macro		OpEBM1		
		@ A is 8bits
		ADD		rscratch,reg_cpu_var,#RAH_ofs
		MOV		reg_a,reg_a, LSR #24
		SWPB		reg_a,reg_a,[rscratch]
		MOVS		reg_a,reg_a, LSL #24
		UPDATE_ZN
		ADD2CYCLE
.endm
.macro		OpEBM0		
		@ A is 16bits
		MOV 		rscratch, reg_a, ROR #24 @  ll0000hh
		ORR 		rscratch, rscratch, reg_a, LSR #8@  ll0000hh + 00hhll00 -> llhhllhh
		MOV 		reg_a, rscratch, LSL #16@  llhhllhh -> llhh0000		
		MOVS		rscratch,rscratch,LSL #24 @ to set Z & N flags with AL		
		UPDATE_ZN
		ADD2CYCLE
.endm


/**********************************************************************************************/
/* RTI *************************************************************************************** */
.macro		Op40X1M1
		@ INDEX set, MEMORY set		
		BIC		rstatus,rstatus,#0xFF000000
		PullBr
		ORR		rstatus,rscratch,rstatus
		PullWlow	rpc
		TST 		rstatus, #MASK_EMUL
		ORRNE		rstatus, rstatus, #(MASK_MEM|MASK_INDEX)
                BNE		2401f
		PullBrLow
		BIC		reg_p_bank,reg_p_bank,#0xFF
		ORR		reg_p_bank,reg_p_bank,rscratch
2401:		
		ADD 		rscratch, rpc, reg_p_bank, LSL #16
		S9xSetPCBase
		TST 		rstatus, #MASK_INDEX		
		@ INDEX cleared & was set : 8->16
		MOVEQ		reg_x,reg_x,LSR #8
		MOVEQ		reg_y,reg_y,LSR #8
		TST 		rstatus, #MASK_MEM		
		@ MEMORY cleared & was set : 8->16
		LDREQB		rscratch,[reg_cpu_var,#RAH_ofs]		
		MOVEQ		reg_a,reg_a,LSR #8		
		ORREQ		reg_a,reg_a,rscratch, LSL #24		
		ADD2CYCLE
		S9xFixCycles
.endm
.macro		Op40X0M1
		@ INDEX cleared, MEMORY set		
		BIC		rstatus,rstatus,#0xFF000000
		PullBr
		ORR		rstatus,rscratch,rstatus
		PullWlow	rpc
		TST 		rstatus, #MASK_EMUL
		ORRNE		rstatus, rstatus, #(MASK_MEM|MASK_INDEX)
                BNE		2401f
		PullBrLow
		BIC		reg_p_bank,reg_p_bank,#0xFF
		ORR		reg_p_bank,reg_p_bank,rscratch
2401:		
		ADD 		rscratch, rpc, reg_p_bank, LSL #16
		S9xSetPCBase		
		TST 		rstatus, #MASK_INDEX		
		@ INDEX set & was cleared : 16->8
		MOVNE		reg_x,reg_x,LSL #8
		MOVNE		reg_y,reg_y,LSL #8		
		TST 		rstatus, #MASK_MEM		
		@ MEMORY cleared & was set : 8->16
		LDREQB		rscratch,[reg_cpu_var,#RAH_ofs]		
		MOVEQ		reg_a,reg_a,LSR #8		
		ORREQ		reg_a,reg_a,rscratch, LSL #24
		ADD2CYCLE
		S9xFixCycles
.endm
.macro		Op40X1M0
		@ INDEX set, MEMORY cleared
		BIC		rstatus,rstatus,#0xFF000000
		PullBr
		ORR		rstatus,rscratch,rstatus
		PullWlow	rpc
		TST 		rstatus, #MASK_EMUL
		ORRNE		rstatus, rstatus, #(MASK_MEM|MASK_INDEX)
                BNE		2401f
		PullBrLow
		BIC		reg_p_bank,reg_p_bank,#0xFF
		ORR		reg_p_bank,reg_p_bank,rscratch
2401:		
		ADD 		rscratch, rpc, reg_p_bank, LSL #16
		S9xSetPCBase
		TST 		rstatus, #MASK_INDEX		
		@ INDEX cleared & was set : 8->16
		MOVEQ		reg_x,reg_x,LSR #8
		MOVEQ		reg_y,reg_y,LSR #8		
		TST 		rstatus, #MASK_MEM		
		@ MEMORY set & was cleared : 16->8
		MOVNE		rscratch,reg_a,LSR #24
		MOVNE		reg_a,reg_a,LSL #8
		STRNEB		rscratch,[reg_cpu_var,#RAH_ofs]
		ADD2CYCLE
		S9xFixCycles
.endm
.macro		Op40X0M0
		@ INDEX cleared, MEMORY cleared
		BIC		rstatus,rstatus,#0xFF000000
		PullBr
		ORR		rstatus,rscratch,rstatus
		PullWlow	rpc
		TST 		rstatus, #MASK_EMUL
		ORRNE		rstatus, rstatus, #(MASK_MEM|MASK_INDEX)
                BNE		2401f
		PullBrLow
		BIC		reg_p_bank,reg_p_bank,#0xFF
		ORR		reg_p_bank,reg_p_bank,rscratch
2401:		
		ADD 		rscratch, rpc, reg_p_bank, LSL #16
		S9xSetPCBase
		TST 		rstatus, #MASK_INDEX
		@ INDEX set & was cleared : 16->8
		MOVNE		reg_x,reg_x,LSL #8
		MOVNE		reg_y,reg_y,LSL #8		
		TST 		rstatus, #MASK_MEM		
		@ MEMORY set & was cleared : 16->8
		@ MEMORY set & was cleared : 16->8
		MOVNE		rscratch,reg_a,LSR #24
		MOVNE		reg_a,reg_a,LSL #8
		STRNEB		rscratch,[reg_cpu_var,#RAH_ofs]
		ADD2CYCLE
		S9xFixCycles
.endm
	

/**********************************************************************************************/
/* STP/WAI/DB ******************************************************************************** */
@  WAI
.macro		OpCB	/*WAI*/
@ cpuops.cpp: CPU.WaitingForInterrupt = TRUE; CPU.PC--; and with
@ Settings.Shutdown on, CPU.Cycles = CPU.NextEvent;
	MOV		rscratch,#1
	SUB		rpc,rpc,#1
	STRB		rscratch,[reg_cpu_var,#WaitingForInterrupt_ofs]
	LDR		reg_cycles,[reg_cpu_var,#NextEvent_ofs]
.endm
.macro		OpDB	/*STP*/
		SUB	rpc,rpc,#1
		LDR	rscratch,[reg_cpu_var,#Flags_ofs]
		ORR	rscratch,rscratch,#1	@ CPU.Flags |= DEBUG_MODE_FLAG;
		STR	rscratch,[reg_cpu_var,#Flags_ofs]
.endm
.macro		Op42   /*Reserved Snes9X*/
@ A one-byte no-op in cpuops.cpp. (psnes used it for SNESAdvance's speed
@ hack branches, which this port doesn't patch in.)
.endm



/**********************************************************************************************/
/* AND ******************************************************************************** */
.macro		Op29M1
		LDRB	rscratch    , [rpc], #1		
		ANDS	reg_a    , reg_a,	rscratch, LSL #24
		UPDATE_ZN
		ADD1MEM
.endm		
.macro		Op29M0		
		LDRB	rscratch2  , [rpc,#1]
		LDRB	rscratch   , [rpc], #2
		ORR	rscratch, rscratch, rscratch2, LSL #8		
		ANDS	reg_a    , reg_a,	rscratch, LSL #16
		UPDATE_ZN
		ADD2MEM
.endm

		


		

		

		

		

		

		
/**********************************************************************************************/
/* EOR ******************************************************************************** */
.macro		Op49M0		
                LDRB	rscratch2 , [rpc, #1]
                LDRB	rscratch , [rpc], #2
		ORR	rscratch, rscratch, rscratch2,LSL #8                
		EORS    reg_a, reg_a, rscratch,LSL #16
		UPDATE_ZN
		ADD2MEM
.endm

		
.macro		Op49M1		
                LDRB	rscratch , [rpc], #1                
		EORS    reg_a, reg_a, rscratch,LSL #24
		UPDATE_ZN
		ADD1MEM
.endm


/**********************************************************************************************/
/* STA *************************************************************************************** */		
.macro		Op81M1				
		STA8
		@ TST 		rstatus, #MASK_INDEX
		@ ADD1CYCLENE
.endm
.macro		Op81M0				
		STA16
		@ TST rstatus, #MASK_INDEX
		@ ADD1CYCLENE
.endm


/**********************************************************************************************/
/* BIT *************************************************************************************** */
.macro		Op89M1		
                LDRB	rscratch , [rpc], #1                
		TST     reg_a, rscratch, LSL #24
		UPDATE_Z
		ADD1MEM
.endm
.macro		Op89M0		
                LDRB	rscratch2 , [rpc, #1]
                LDRB	rscratch , [rpc], #2
		ORR	rscratch, rscratch, rscratch2, LSL #8                
		TST     reg_a, rscratch, LSL #16
		UPDATE_Z
		ADD2MEM
.endm

		

		
		

/**********************************************************************************************/
/* LDY *************************************************************************************** */
.macro		OpA0X1
                LDRB	rscratch , [rpc], #1                
                MOVS    reg_y, rscratch, LSL #24
		UPDATE_ZN
		ADD1MEM
.endm
.macro		OpA0X0		
                LDRB	rscratch2 , [rpc, #1]
                LDRB	rscratch , [rpc], #2
		ORR	rscratch, rscratch, rscratch2, LSL #8                
                MOVS    reg_y, rscratch, LSL #16
		UPDATE_ZN
		ADD2MEM
.endm

/**********************************************************************************************/
/* LDX *************************************************************************************** */		
.macro		OpA2X1		
                LDRB	rscratch , [rpc], #1                
                MOVS    reg_x, rscratch, LSL #24
		UPDATE_ZN
		ADD1MEM
.endm
.macro		OpA2X0		
                LDRB	rscratch2 , [rpc, #1]
                LDRB	rscratch , [rpc], #2
		ORR	rscratch, rscratch, rscratch2, LSL #8                
                MOVS    reg_x, rscratch, LSL #16
		UPDATE_ZN
		ADD2MEM
.endm
		
/**********************************************************************************************/
/* LDA *************************************************************************************** */		
.macro		OpA9M1		
                LDRB	rscratch , [rpc], #1
                MOVS    reg_a, rscratch, LSL #24
		UPDATE_ZN
		ADD1MEM
.endm
.macro		OpA9M0		
                LDRB	rscratch2 , [rpc, #1]
                LDRB	rscratch , [rpc], #2
		ORR	rscratch, rscratch, rscratch2, LSL #8                
                MOVS    reg_a, rscratch, LSL #16                
		UPDATE_ZN
		ADD2MEM
.endm
												
/**********************************************************************************************/
/* CMY *************************************************************************************** */
.macro		OpC0X1
		LDRB	rscratch    , [rpc], #1		
		SUBS	rscratch2   , reg_y , rscratch, LSL #24
		BICCC	rstatus, rstatus, #MASK_CARRY
		ORRCS	rstatus, rstatus, #MASK_CARRY
		UPDATE_ZN		
		ADD1MEM
.endm
.macro		OpC0X0
		LDRB	rscratch2   , [rpc, #1]
		LDRB	rscratch   , [rpc], #2		
		ORR	rscratch, rscratch, rscratch2, LSL #8
		SUBS	rscratch2   , reg_y, rscratch, LSL #16
		BICCC	rstatus, rstatus, #MASK_CARRY
		ORRCS	rstatus, rstatus, #MASK_CARRY
		UPDATE_ZN
		ADD2MEM
.endm

		

		

/**********************************************************************************************/
/* CMP *************************************************************************************** */		
.macro		OpC9M1		
		LDRB	rscratch    , [rpc], #1		
		SUBS	rscratch2   , reg_a , rscratch, LSL #24		
		BICCC	rstatus, rstatus, #MASK_CARRY
		ORRCS	rstatus, rstatus, #MASK_CARRY
		UPDATE_ZN
		ADD1MEM
.endm
.macro		OpC9M0		
		LDRB	rscratch2   , [rpc,#1]
		LDRB	rscratch   , [rpc], #2		
		ORR	rscratch, rscratch, rscratch2, LSL #8
		SUBS	rscratch2   , reg_a, rscratch, LSL #16		
		BICCC	rstatus, rstatus, #MASK_CARRY
		ORRCS	rstatus, rstatus, #MASK_CARRY
		UPDATE_ZN
		ADD2MEM
.endm

/**********************************************************************************************/
/* CMX *************************************************************************************** */		
.macro		OpE0X1		
		LDRB	rscratch    , [rpc], #1		
		SUBS	rscratch2   , reg_x , rscratch, LSL #24
		BICCC	rstatus, rstatus, #MASK_CARRY
		ORRCS	rstatus, rstatus, #MASK_CARRY
		UPDATE_ZN		
		ADD1MEM
.endm
.macro		OpE0X0		
		LDRB	rscratch2   , [rpc,#1]
		LDRB	rscratch   , [rpc], #2		
		ORR	rscratch, rscratch, rscratch2, LSL #8
		SUBS	rscratch2   , reg_x, rscratch, LSL #16
		BICCC	rstatus, rstatus, #MASK_CARRY

		ORRCS	rstatus, rstatus, #MASK_CARRY
		UPDATE_ZN
		ADD2MEM
.endm

