// main.c — "1 2 3 [Tick]"을 50번 정확히 출력 (메인만 출력)
// "[Tick]" 출력 순간에만 IRQ+FIQ를 잠깐 마스크해서 출력 깨짐 방지

#include <stdint.h>
#include <stddef.h>

#define UART0_BASE   0x09000000UL
#define UART_DR      (*(volatile uint32_t *)(UART0_BASE + 0x00))
#define UART_FR      (*(volatile uint32_t *)(UART0_BASE + 0x18))
#define UART_FR_TXFF (1u << 5)

static inline void uart_putc(char c){
    while (UART_FR & UART_FR_TXFF) {}
    UART_DR = (uint32_t)c;
}
static inline void uart_puts(const char*s){
    while(*s) uart_putc(*s++);
}

/* 간단 메모리 배리어/이벤트 */
static inline void dmb_ish(void){ __asm__ volatile("dmb ish" ::: "memory"); }
static inline void sev(void){ __asm__ volatile("sev" ::: "memory"); }
static inline void wfe(void){ __asm__ volatile("wfe" ::: "memory"); }

/* DAIF 저장/복구 + IRQ(F)+FIQ(I) 마스크 */
static inline uint64_t daif_read(void){ uint64_t v; __asm__ volatile("mrs %0, DAIF":"=r"(v)); return v; }
static inline void daif_write(uint64_t v){ __asm__ volatile("msr DAIF, %0"::"r"(v)); }
static inline void daif_mask_if(void){ __asm__ volatile("msr DAIFSet, #3"); }   // I(0x2)+F(0x1) 마스크
// 복구는 daif_write(old) 사용

typedef struct { uint64_t x[31]; uint64_t elr_el1; uint64_t spsr_el1; } os_context_t;

/* interrupt_asm.S에서 참조하지만, 지금은 컨텍스트 스위칭을 하지 않음 */
os_context_t* os_schedule_from_isr(os_context_t *current){ return current; }

/* ISR와 공유하는 플래그 */
volatile int g_tick_req = 0;  // 메인이 요청
volatile int g_tick_ack = 0;  // ISR이 접수 표시

int main(void){
    for (int n=0; n<50; n++){
        uart_putc('1'); uart_putc(' ');
        for (volatile uint32_t i=0;i<200000;i++){}

        uart_putc('2'); uart_putc(' ');
        for (volatile uint32_t i=0;i<200000;i++){}

        uart_putc('3'); uart_putc(' ');
        for (volatile uint32_t i=0;i<200000;i++){}

        // 이번 사이클 틱 1회 요청
        g_tick_req = 1;
        dmb_ish();
        sev();  // 굳이 필요하진 않지만, 대기 중이면 깨우기

        // ISR이 요청을 접수할 때까지 잠깐 대기
        while (!g_tick_ack) { wfe(); }

        // 접수 표시 초기화
        g_tick_ack = 0;
        dmb_ish();

        // ★ 아주 짧은 출력 구간만 IRQ+FIQ 모두 마스크해서 깨짐 방지
        uint64_t daif_old = daif_read();
        daif_mask_if();                // I+F 마스크
        uart_putc(' '); uart_putc('[');
        uart_putc('T'); uart_putc('i');
        uart_putc('c'); uart_putc('k');
        uart_putc(']');
        daif_write(daif_old);          // 원래 상태로 복구
    }

    uart_puts("\nDone.\n");
    for(;;)__asm__ volatile("wfi");
}
