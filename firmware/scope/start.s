.text
	.arm

	.global	_start
_start:
	//Boot head information for BROM
	.long 0xea000006
	.byte 'e', 'G', 'O', 'N', '.', 'E', 'X', 'E'
	.long 0, __program_size
	.byte 'E', 'X', 'E', 'C'
	.long 0, 0

_reset:
	//PC protocol: restart record in DDR outside the image, BSS and stacks (pc_hal_scope.c pc_restart_*). LR, SP and
	//CPSR as found at this entry: after a jump to 0 / 0x80000000 (NULL function call, corrupt return address) LR
	//points just past the culprit. Cold boots are told apart in C by the alive magic of the previous run.
	ldr r12, =0x81A00000
	str lr, [r12, #8]
	str sp, [r12, #12]
	mrs r11, cpsr
	str r11, [r12, #16]
#if PC_MMU
	//fw 0.2.48: leave the MMU and caches as a cold boot finds them (cleaned first if they were on: .data is not
	//reloaded on a restart, so its last values must reach DDR). IRQ / FIQ off: a restart may come from SVC mode.
	mrs r0, cpsr
	orr r0, r0, #0xC0
	msr cpsr_c, r0
	bl pc_mmu_off
#endif
	//Initialize stacks. Interrupts disabled
	mrs r0, cpsr
	bic r0, r0, #0x1f
	orr r0, r0, #0xdb
	msr cpsr, r0
	ldr sp, _stack_und_end

	mrs r0, cpsr
	bic r0, r0, #0x1f
	orr r0, r0, #0xd7
	msr cpsr, r0
	ldr sp, _stack_abt_end

	mrs r0, cpsr
	bic r0, r0, #0x1f
	orr r0, r0, #0xd2
	msr cpsr, r0
	ldr sp, _stack_irq_end

	mrs r0, cpsr
	bic r0, r0, #0x1f
	orr r0, r0, #0xd1
	msr cpsr, r0
	ldr sp, _stack_fiq_end

	mrs r0, cpsr
	bic r0, r0, #0x1f
	orr r0, r0, #0xdf
	msr cpsr, r0
	ldr sp, _stack_sys_end

	mrs r0, cpsr
	bic r0, r0, #0x1f
	orr r0, r0, #0xd3
	msr cpsr, r0
	ldr sp, _stack_srv_end

	//Set vector to the low address
	mrc p15, 0, r0, c1, c0, 0
	bic r0, #0x2000
	mcr p15, 0, r0, c1, c0, 0

  //Copy vector to the correct address
  adr   r0, _vector
	mrc   p15, 0, r2, c1, c0, 0
	ands  r2, r2, #(1 << 13)
	ldreq r1, =0x00000000
	ldrne r1, =0xffff0000
  ldmia r0!, {r2-r8, r10}
  stmia r1!, {r2-r8, r10}
  ldmia r0!, {r2-r8, r10}
  stmia r1!, {r2-r8, r10}

  //Clear the used variables


	//Call main
	ldr r1, = main
	mov pc, r1

_vector:
	ldr pc, _jump_reset
	ldr pc, _undefined_instruction
	ldr pc, _software_interrupt
	ldr pc, _prefetch_abort
	ldr pc, _data_abort
	ldr pc, _not_used
	ldr pc, _irq
	ldr pc, _fiq

_jump_reset:
  .word _reset
_undefined_instruction:
	.word undefined_instruction
_software_interrupt:
	.word software_interrupt
_prefetch_abort:
	.word prefetch_abort
_data_abort:
	.word data_abort
_not_used:
	.word not_used
_irq:
	.word irq
_fiq:
	.word fiq

//Not used exception handlers
//PC protocol: record the exception (type, LR, SPSR, SP of that mode; since fw 0.2.48 also DFSR, IFSR and FAR at
//+40 / +44 / +48) and restart instead of hanging, so the host sees the device come back and can read where it crashed
//(GET_LINK_STATS restart counters 49-56 and 58-59, event type 15)
undefined_instruction:
	mov r11, #1
	b pc_exception_common

software_interrupt:
	mov r11, #2
	b pc_exception_common

prefetch_abort:
	mov r11, #3
	b pc_exception_common

data_abort:
	mov r11, #4
	b pc_exception_common

not_used:
	mov r11, #5
	b pc_exception_common

pc_exception_common:
	ldr r12, =0x81A00000
	str r11, [r12, #20]
	str lr, [r12, #24]
	mrs r10, spsr
	str r10, [r12, #28]
	str sp, [r12, #32]
	mrc p15, 0, r10, c5, c0, 0
	str r10, [r12, #40]
	mrc p15, 0, r10, c5, c0, 1
	str r10, [r12, #44]
	mrc p15, 0, r10, c6, c0, 0
	str r10, [r12, #48]
	ldr r10, =0x45584321
	str r10, [r12, #36]
	b _reset

#if PC_MMU
//fw 0.2.48 (docs/plans/mmu-dcache.md): MMU, D-cache and I-cache off, as a cold boot finds them. Called from _reset and
//before the FEL jump, IRQs masked. With the MMU on the whole D-cache is cleaned and invalidated first (test, clean and
//invalidate loop); with it off only invalidated: nothing ran cacheable, and cleaning lines of unknown content could
//write over a freshly loaded image. No stack; clobbers r0 and the flags.
	.global pc_mmu_off
pc_mmu_off:
	mrc p15, 0, r0, c1, c0, 0
	tst r0, #1
	beq 2f
1:
	mrc p15, 0, APSR_nzcv, c7, c14, 3
	bne 1b
2:
	mov r0, #0
	mcr p15, 0, r0, c7, c10, 4
	mrc p15, 0, r0, c1, c0, 0
	bic r0, r0, #0x1000
	bic r0, r0, #0x0005
	mcr p15, 0, r0, c1, c0, 0
	mrc p15, 0, r0, c1, c0, 0
	mov r0, #0
	mcr p15, 0, r0, c7, c7, 0
	mcr p15, 0, r0, c8, c7, 0
	bx lr
#endif

irq:
	ldr sp, _stack_irq_end
	sub sp, sp, #72
	stmia sp, {r0 - r12}
	add r8, sp, #60
	stmdb r8, {sp, lr}^
	str lr, [r8, #0]
	mrs r6, spsr
	str r6, [r8, #4]
	str r0, [r8, #8]
	mov r0, sp
	bl irq_handler
	ldmia sp, {r0 - lr}^
	mov r0, r0
	ldr lr, [sp, #60]
	add sp, sp, #72
	subs pc, lr, #4

fiq:
  mov r11, #6
  b pc_exception_common

//The location of the stacks section
_stack_und_end:
	.long 0x81FB5000
_stack_abt_end:
	.long 0x81FC1800
_stack_irq_end:
	.long 0x81FDA800
_stack_fiq_end:
	.long 0x81FE7000
_stack_sys_end:
	.long 0x82000000
_stack_srv_end:
	.long 0x81FF3800
