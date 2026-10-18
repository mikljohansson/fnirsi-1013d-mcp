/*
 * strlen.s
 */
	.text

    .global strlen
    .type strlen, %function
    .align 4

// fnirsi-1013d-ai: the original loop label sat on "mov r1, r0", so it
// returned a pointer to the terminator instead of the length. Upstream never
// linked this file; it is needed now because GCC >= 12 turns counting loops
// (my_strlen, test.c write_text) into strlen calls.
strlen:
    mov   r1, r0
1:  ldrb  r3, [r1], #1
    cmp   r3, #0
    bne   1b
    sub   r0, r1, r0
    sub   r0, r0, #1
    mov   pc, lr



