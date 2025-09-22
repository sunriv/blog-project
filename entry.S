    .section .text.boot
    .global _start

    .set UART0_BASE, 0x09000000
    .set UART_DR,   0x00
    .set UART_FR,   0x18
    .set TXFF_BIT,  5

_start:
    // SP를 SP_EL1로 사용 (CurrentEL=EL1 가정)
    msr     SPSel, #1

    // 모든 예외 일단 막기(DAIF=1111): 초기화 중 안전
    msr     DAIFSet, #0xF

    // 스택 초기화 (linker.ld에서 __stack_top 16바이트 정렬 보장)
    ldr     x0, =__stack_top
    mov     sp, x0

    // BSS 클리어
    ldr     x1, =__bss_start
    ldr     x2, =__bss_end
1:  cmp     x1, x2
    b.hs    2f
    str     xzr, [x1], #8
    b       1b
2:
    // VBAR 설정
    adrp    x0, vectors_el1
    add     x0, x0, :lo12:vectors_el1
    msr     VBAR_EL1, x0
    isb

    // (선택) 간단한 UART Hello (동작 점검용)
    adrp    x12, hello_str
    add     x12, x12, :lo12:hello_str
0:  ldrb    w13, [x12], #1
    cbz     w13, 9f
1:  ldr     x14, =UART0_BASE
    ldr     w15, [x14, #UART_FR]
    tbnz    w15, #TXFF_BIT, 1b
    str     w13, [x14, #UART_DR]
    b       0b
9:

    // GIC / Timer 초기화 (C 함수)
    bl      gic_init
    bl      timer_init_100hz

    // 이제 IRQ 허용(DAIF의 I비트만 클리어)
    msr     DAIFClr, #2
    isb

    // 커널/스케줄러 진입 (원하면 main으로 교체)
    bl      main

hang:
    wfi
    b       hang

// ---------------- 데이터 ----------------
    .section .rodata
    .align 4
hello_str:
    .asciz "Hello, World!\n"

    .section .text
    .globl __bss_start
    .globl __bss_end
    .globl __stack_top

// =========================================================
//   하드웨어 예외 벡터 테이블 (EL1) — irq는 래퍼로 직결
//   interrupt_asm.S의 irq_el1h 라벨과 연결됨
// =========================================================
    .section .vectors, "ax"
    .align  11
    .global vectors_el1
vectors_el1:
    // Current EL, SP0
    b   el1_sync_sp0              // 0x000
    b   el1_irq_sp0               // 0x080
    b   el1_fiq_sp0               // 0x100
    b   el1_serr_sp0              // 0x180

    // Current EL, SPx (보통 SP_EL1)
    .org vectors_el1 + 0x200
    b   el1_sync_spx
    .org vectors_el1 + 0x280
    b   el1_irq_spx               // ★ 여기서 래퍼로 점프
    .org vectors_el1 + 0x300
    b   el1_fiq_spx
    .org vectors_el1 + 0x380
    b   el1_serr_spx

    // Lower EL AArch64
    .org vectors_el1 + 0x400
    b   el1_sync_el0
    .org vectors_el1 + 0x480
    b   el1_irq_el0
    .org vectors_el1 + 0x500
    b   el1_fiq_el0
    .org vectors_el1 + 0x580
    b   el1_serr_el0

    // Lower EL AArch32
    .org vectors_el1 + 0x600
    b   el1_sync_a32
    .org vectors_el1 + 0x680
    b   el1_irq_a32
    .org vectors_el1 + 0x700
    b   el1_fiq_a32
    .org vectors_el1 + 0x780
    b   el1_serr_a32

    .section .text
    .p2align 4

    // === 스텁들 ===
el1_sync_sp0:  wfi; b el1_sync_sp0
el1_irq_sp0:   wfi; b el1_irq_sp0
el1_fiq_sp0:   wfi; b el1_fiq_sp0
el1_serr_sp0:  wfi; b el1_serr_sp0

el1_sync_spx:  wfi; b el1_sync_spx

    // ★ IRQ(EL1h) → 레지스터 전체 저장/복구 래퍼( interrupt_asm.S )
    .extern irq_el1h
el1_irq_spx:
    b   irq_el1h

el1_fiq_spx:   wfi; b el1_fiq_spx
el1_serr_spx:  wfi; b el1_serr_spx

el1_sync_el0:  wfi; b el1_sync_el0
el1_irq_el0:   wfi; b el1_irq_el0
el1_fiq_el0:   wfi; b el1_fiq_el0
el1_serr_el0:  wfi; b el1_serr_el0

el1_sync_a32:  wfi; b el1_sync_a32
el1_irq_a32:   wfi; b el1_irq_a32
el1_fiq_a32:   wfi; b el1_fiq_a32
el1_serr_a32:  wfi; b el1_serr_a32
