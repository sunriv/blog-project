// context.S (AArch64)
// 협력식 컨텍스트 스위칭: callee-saved(x19..x30)만 저장/복구.
// 프리엠프티브 경로는 interrupt_asm.S의 트랩 프레임을 사용.

    .section .text, "ax"
    .align  4

    .global _os_save_context
    .type   _os_save_context, %function
// 반환값 x0 = 저장된 컨텍스트 프레임의 SP
_os_save_context:
    // callee-saved: x19..x29, lr(x30)
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    stp x29, x30, [sp, #-16]!
    mov x0, sp
    ret

    .global _os_restore_context
    .type   _os_restore_context, %function
// 인자 x0 = 복구할 컨텍스트 프레임의 SP
_os_restore_context:
    mov sp, x0
    ldp x29, x30, [sp], #16
    ldp x27, x28, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ret
