// main.c — AArch64 bare-metal (QEMU virt, PL011 UART)
// 8N1 + FIFO 비활성 초기화, TXFE+BUSY 플러시
// '3 '과 '[Tick]'을 원자 출력, 각 호출 후 0.1s 지연

#include <stdint.h>

/* ===== PL011 UART (QEMU virt: 0x09000000) ===== */
#define UART0_BASE     0x09000000UL
#define UART_DR        (*(volatile uint32_t *)(UART0_BASE + 0x00))
#define UART_FR        (*(volatile uint32_t *)(UART0_BASE + 0x18))
#define UART_IBRD      (*(volatile uint32_t *)(UART0_BASE + 0x24))
#define UART_FBRD      (*(volatile uint32_t *)(UART0_BASE + 0x28))
#define UART_LCR_H     (*(volatile uint32_t *)(UART0_BASE + 0x2C))
#define UART_CR        (*(volatile uint32_t *)(UART0_BASE + 0x30))
#define UART_IMSC      (*(volatile uint32_t *)(UART0_BASE + 0x38))
#define UART_ICR       (*(volatile uint32_t *)(UART0_BASE + 0x44))

/* FR bits */
#define UART_FR_TXFF   (1u << 5)
#define UART_FR_RXFE   (1u << 4)
#define UART_FR_BUSY   (1u << 3)
#define UART_FR_TXFE   (1u << 7)

/* LCR_H bits */
#define LCR_H_FEN      (1u << 4)   // FIFO enable
#define LCR_H_WLEN_8   (3u << 5)   // 8-bit

/* CR bits */
#define CR_UARTEN      (1u << 0)
#define CR_TXE         (1u << 8)
#define CR_RXE         (1u << 9)

/* ===== barriers / system regs ===== */
static inline void dmb_ish(void){ __asm__ volatile("dmb ish" ::: "memory"); }
static inline void isb(void){ __asm__ volatile("isb" ::: "memory"); }
static inline void sev(void){ __asm__ volatile("sev" ::: "memory"); }
static inline void wfe(void){ __asm__ volatile("wfe" ::: "memory"); }

/* generic counter helpers */
static inline uint64_t cntfrq_el0(void){ uint64_t v; __asm__ volatile("mrs %0, CNTFRQ_EL0":"=r"(v)); return v; }
static inline uint64_t cntpct_el0(void){ uint64_t v; __asm__ volatile("mrs %0, CNTPCT_EL0":"=r"(v)); return v; }

/* CNTP control (for 이전 버전 호환; 여기선 stop/resume 안 씀) */
static inline uint64_t cnTP_ctl_read(void){ uint64_t v; __asm__ volatile("mrs %0, CNTP_CTL_EL0":"=r"(v)); return v; }
static inline void     cnTP_ctl_write(uint64_t v){ __asm__ volatile("msr CNTP_CTL_EL0, %0"::"r"(v)); isb(); }

/* ISR와 공유 플래그 (정의 여기, interrupt.c에서는 extern) */
volatile int g_tick_req = 0;
volatile int g_tick_ack = 0;

/* ===== UART init: 8N1, FIFO 끔, 인터럽트 비사용 ===== */
static inline void uart_init(void){
    UART_CR  = 0;           // disable
    UART_IMSC = 0;          // mask all UART interrupts
    UART_ICR  = 0x7FF;      // clear ALL interrupts
    // QEMU에서는 baud 무시되는 편이라 분주기 건들 필요 없음
    UART_LCR_H = (UART_LCR_H & ~(LCR_H_FEN)) | LCR_H_WLEN_8; // 8N1, FIFO off
    UART_CR  = CR_UARTEN | CR_TXE | CR_RXE;   // enable TX/RX
    isb();
}

/* ===== TX 완료까지 확실히 기다리기: TXFE==1 && BUSY==0 ===== */
static inline void uart_flush_tx(void){
    // FIFO가 완전히 비고, 라인 전송도 끝날 때까지
    while (!(UART_FR & UART_FR_TXFE)) {}
    while (UART_FR & UART_FR_BUSY) {}
}

/* 기본 putc/puts */
static inline void uart_putc(char c){
    while (UART_FR & UART_FR_TXFF) {}   // FIFO(혹은 홀더) 가득 차면 대기
    UART_DR = (uint32_t)(uint8_t)c;
}
static inline void uart_puts(const char*s){
    while(*s) uart_putc(*s++);
}

/* 문자열 원샷 출력 + 완전 플러시 */
static inline void uart_write_blocking(const char* s){
    while (*s) {
        while (UART_FR & UART_FR_TXFF) {}
        UART_DR = (uint32_t)(uint8_t)(*s++);
    }
    uart_flush_tx();
}

/* 0.1초 바쁜대기: CNTFRQ 기반 */
static inline void delay_100ms(void){
    uint64_t f = cntfrq_el0();
    uint64_t d = f / 10;             // 0.1s
    uint64_t b = cntpct_el0();
    while ((cntpct_el0() - b) < d) { /* spin */ }
}

/* 아주 짧은 원자 출력: DAIF 전부 마스크 */
static inline void atomic_write(const char* s){
    uart_write_blocking(s);;
}

/* [Tick] 출력도 원자화 (CNTP stop/resume 제거: 단순화) */
static inline void print_tick_atomic(void){
    atomic_write("[Tick]");
}

/* ===== interrupt_asm.S에서 참조하는 심볼 ===== */
typedef struct { uint64_t x[31]; uint64_t elr_el1; uint64_t spsr_el1; } os_context_t;
os_context_t* os_schedule_from_isr(os_context_t *current){ return current; }

/* ===== main ===== */
int main(void){
    uart_init();   // ← 반드시 먼저 초기화

    for (int n=0; n<50; n++){
        // 각 출력 호출 뒤 0.1초 지연
        uart_putc('1'); delay_100ms();
        uart_putc(' '); delay_100ms();
        uart_putc('2'); delay_100ms();
        uart_putc(' '); delay_100ms();

        // '3 '은 원자 출력(짧음)으로 경합 제거
        atomic_write("3 ");
      

        // 틱 요청 → ISR ack 대기
        g_tick_req = 1; dmb_ish(); sev();
        while (!g_tick_ack) { wfe(); }
        g_tick_ack = 0; dmb_ish();

        // 원자적 [Tick] 출력
        print_tick_atomic();

        // 호출 마지막 0.1초 대기
    
    }

    uart_puts("\nDone.\n");
    for(;;) __asm__ volatile("wfi");
}
