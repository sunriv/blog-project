// interrupt_asm.S — AArch64 EL1 IRQ wrapper
// 저장 레이아웃: os_context_t와 동일
//  [0..30] : x0..x30  (각 8바이트, x30의 오프셋 = 30*8)
//  [31]    : ELR_EL1
//  [32]    : SPSR_EL1
// 총 33 * 8 = 264바이트

    .section .text, "ax"
    .align  4

    .global irq_el1h
    .type   irq_el1h, %function
    .extern irq_handler_c

irq_el1h:
    // ===== 프롤로그: 레지스터/상태 저장 =====
    sub     sp, sp, #(33*8)           // 264 bytes

    // x0..x29
    stp     x0,  x1,  [sp, #(0*16)]
    stp     x2,  x3,  [sp, #(1*16)]
    stp     x4,  x5,  [sp, #(2*16)]
    stp     x6,  x7,  [sp, #(3*16)]
    stp     x8,  x9,  [sp, #(4*16)]
    stp     x10, x11, [sp, #(5*16)]
    stp     x12, x13, [sp, #(6*16)]
    stp     x14, x15, [sp, #(7*16)]
    stp     x16, x17, [sp, #(8*16)]
    stp     x18, x19, [sp, #(9*16)]
    stp     x20, x21, [sp, #(10*16)]
    stp     x22, x23, [sp, #(11*16)]
    stp     x24, x25, [sp, #(12*16)]
    stp     x26, x27, [sp, #(13*16)]
    stp     x28, x29, [sp, #(14*16)]

    // x30, ELR, SPSR
    str     x30,       [sp, #(30*8)]      // x[30]
    mrs     x9,  ELR_EL1
    mrs     x10, SPSR_EL1
    str     x9,        [sp, #(31*8)]      // elr_el1
    str     x10,       [sp, #(32*8)]      // spsr_el1

    // ===== C 핸들러 호출 (SP 16바이트 정렬 보장) =====
    mov     x0, sp                         // x0 = 현재 프레임 포인터
    sub     sp, sp, #8                     // 264 저장으로 SP가 8 어긋남 → 임시로 -8 해서 16정렬
    bl      irq_handler_c                  // os_context_t* irq_handler_c(os_context_t *current)
    add     sp, sp, #8                     // 정렬 복구

    // x0 = 복구할(다음) 프레임 포인터
    mov     sp, x0

    // ===== 에필로그: 레지스터/상태 복구 =====
    ldp     x0,  x1,  [sp, #(0*16)]
    ldp     x2,  x3,  [sp, #(1*16)]
    ldp     x4,  x5,  [sp, #(2*16)]
    ldp     x6,  x7,  [sp, #(3*16)]
    ldp     x8,  x9,  [sp, #(4*16)]
    ldp     x10, x11, [sp, #(5*16)]
    ldp     x12, x13, [sp, #(6*16)]
    ldp     x14, x15, [sp, #(7*16)]
    ldp     x16, x17, [sp, #(8*16)]
    ldp     x18, x19, [sp, #(9*16)]
    ldp     x20, x21, [sp, #(10*16)]
    ldp     x22, x23, [sp, #(11*16)]
    ldp     x24, x25, [sp, #(12*16)]
    ldp     x26, x27, [sp, #(13*16)]
    ldp     x28, x29, [sp, #(14*16)]
    ldr     x30,       [sp, #(30*8)]

    ldr     x9,        [sp, #(31*8)]      // elr
    ldr     x10,       [sp, #(32*8)]      // spsr
    msr     ELR_EL1, x9
    msr     SPSR_EL1, x10

    add     sp, sp, #(33*8)
    eret

// ===== IRQ 마스크/언마스크/복구 (x86의 CLI/STI/restore 대체) =====

    .global eos_disable_interrupt
    .type   eos_disable_interrupt, %function
// return x0 = 이전 DAIF
eos_disable_interrupt:
    mrs     x0, DAIF
    msr     DAIFSet, #2                   // I-bit set (IRQ mask)
    ret

    .global eos_enable_interrupt
    .type   eos_enable_interrupt, %function
eos_enable_interrupt:
    msr     DAIFClr, #2                   // I-bit clear (IRQ unmask)
    ret

    .global eos_restore_interrupt
    .type   eos_restore_interrupt, %function
// arg x0 = 복구할 DAIF
eos_restore_interrupt:
    msr     DAIF, x0
    ret
