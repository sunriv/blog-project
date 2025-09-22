// main.c — 1 2 3을 50번 출력 + IRQ 허용 직후 레지스터 덤프

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

typedef struct { uint64_t x[31]; uint64_t elr_el1; uint64_t spsr_el1; } os_context_t;
extern void * _os_create_context(void *stack_base, size_t stack_size, void (*entry)(void*), void*);
extern os_context_t* os_schedule_from_isr(os_context_t *current);

/* 진단 출력 함수(interrupt.c) */
extern void diag_dump(const char* tag);

static __attribute__((aligned(16))) uint8_t tick_stack[4096];
static os_context_t *g_main_sp=NULL, *g_tick_sp=NULL;
static int g_tick_on=0, g_tick_init=0;

static void tick_entry(void *arg){
    (void)arg;
    for(;;){
        uart_puts(" [Tick]");
        for(volatile uint32_t i=0;i<50000;i++){}
    }
}

os_context_t* os_schedule_from_isr(os_context_t *current){
    if (!g_tick_init){
        g_tick_sp = (os_context_t*)_os_create_context(
            tick_stack, sizeof(tick_stack), tick_entry, NULL
        );
        g_tick_init=1;
    }
    if (!g_tick_on){ g_main_sp=current; g_tick_on=1; return g_tick_sp; }
    g_tick_sp=current; g_tick_on=0; return g_main_sp;
}

int main(void){
    /* entry.S에서 msr DAIFClr,#2 한 직후 시점: 실제 IRQ 언마스크 상태 확인 */
    diag_dump("post-DAIFClr (IRQ unmasked)");

    for (int n=0;n<50;n++){
        uart_putc('1'); uart_putc(' ');
        for (volatile uint32_t i=0;i<200000;i++){}
        uart_putc('2'); uart_putc(' ');
        for (volatile uint32_t i=0;i<200000;i++){}
        uart_putc('3'); uart_putc(' ');
        for (volatile uint32_t i=0;i<200000;i++){}
    }
    uart_puts("\nDone.\n");
    for(;;)__asm__ volatile("wfi");
}
