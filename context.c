// context.c (AArch64 bare-metal)
// 태스크 시작 시 "eret"로 점프할 수 있도록 '가짜 예외 프레임'을 스택에 만들어 둡니다.

#include <stdint.h>
#include <stddef.h>

typedef uint64_t u64;

// 인터럽트 래퍼에서 사용하는 통일된 트랩/컨텍스트 프레임 포맷
// x0..x30 + ELR_EL1 + SPSR_EL1 (총 33개 u64)
typedef struct {
    u64 x[31];       // x0..x30
    u64 elr_el1;     // return PC
    u64 spsr_el1;    // saved PSTATE
} os_context_t;

// 외부에서 참조할 수 있게 심볼 공개
extern void print_context(void *ctx); // 필요 시 구현, 여기서는 빈 껍데기
void print_context(void *ctx) {(void)ctx;}

// EL1h, IRQ 허용된 기본 PSTATE (DAIF 모두 클리어, EL1h=0b0101)
static inline u64 default_spsr_el1(void) {
    // D,A,I,F = 0 (인터럽트 허용), M=0b0101(EL1h)
    return 0x00000005ULL;
}

// stack_base: 스택 바닥 주소(낮은 주소), stack_size: 크기
// entry(arg) 형태로 시작할 태스크의 초기 프레임을 구성하여, "복원용 SP"를 반환
void * _os_create_context(void *stack_base, size_t stack_size,
                          void (*entry)(void *), void *arg)
{
    u64 *sp = (u64 *)((u64)stack_base + stack_size);

    // os_context_t 크기만큼 공간 확보
    sp -= (sizeof(os_context_t) / sizeof(u64));
    os_context_t *ctx = (os_context_t *)sp;

    // 레지스터 클리어
    for (int i = 0; i < 31; ++i) ctx->x[i] = 0;

    // x0 = arg (AAPCS64에 따라 첫 인자)
    ctx->x[0]   = (u64)arg;
    // LR(x30)는 쓰지 않음. eret로 복귀할 것이기 때문.
    // 복귀 PC = 태스크 엔트리
    ctx->elr_el1  = (u64)entry;
    // PSTATE
    ctx->spsr_el1 = default_spsr_el1();

    return (void *)sp; // 이 값을 TCB->sp 같은 데에 저장
}
