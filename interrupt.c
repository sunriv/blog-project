// interrupt.c — AArch64 bare-metal (QEMU virt, GICv2)
// CNTP (Non-secure Physical Timer, PPI 30) 사용.
// 스푸리어스(1020~1023) 무시, 불필요 PPI 전부 disable/clear.

#include <stdint.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t  u8;

/* ---------- UART ---------- */
#define UART0_BASE   0x09000000UL
#define UART_DR      (*(volatile u32 *)(UART0_BASE + 0x00))
#define UART_FR      (*(volatile u32 *)(UART0_BASE + 0x18))
#define UART_FR_TXFF (1u << 5)
static inline void uart_putc(char c){ while (UART_FR & UART_FR_TXFF){} UART_DR = (u32)c; }
static inline void uart_puts(const char*s){ while(*s) uart_putc(*s++); }
static inline void uart_putu(u32 v){
    char buf[12]; int i=0; if(!v){ uart_putc('0'); return; }
    while(v && i<11){ buf[i++] = '0'+(v%10); v/=10; }
    while(i--) uart_putc(buf[i]);
}

/* ---------- GICv2 MMIO base (QEMU virt) ---------- */
#define GICD_BASE   0x08000000UL
#define GICC_BASE   0x08010000UL

/* Distributor */
#define GICD_CTLR           (*(volatile u32 *)(GICD_BASE + 0x000))
#define GICD_IGROUPR(n)     (*(volatile u32 *)(GICD_BASE + 0x080 + 4*(n)))
#define GICD_ISENABLER(n)   (*(volatile u32 *)(GICD_BASE + 0x100 + 4*(n)))
#define GICD_ICENABLER(n)   (*(volatile u32 *)(GICD_BASE + 0x180 + 4*(n)))
#define GICD_ISPENDR(n)     (*(volatile u32 *)(GICD_BASE + 0x200 + 4*(n)))
#define GICD_ICPENDR(n)     (*(volatile u32 *)(GICD_BASE + 0x280 + 4*(n)))
#define GICD_ISACTIVER(n)   (*(volatile u32 *)(GICD_BASE + 0x300 + 4*(n)))
#define GICD_ICACTIVER(n)   (*(volatile u32 *)(GICD_BASE + 0x380 + 4*(n)))
#define GICD_IPRIORITYR(n)  (*(volatile u8  *)(GICD_BASE + 0x400 + (n)))

/* CPU Interface */
#define GICC_CTLR           (*(volatile u32 *)(GICC_BASE + 0x000))
#define GICC_PMR            (*(volatile u32 *)(GICC_BASE + 0x004))
#define GICC_BPR            (*(volatile u32 *)(GICC_BASE + 0x008))
#define GICC_IAR            (*(volatile u32 *)(GICC_BASE + 0x00C))
#define GICC_EOIR           (*(volatile u32 *)(GICC_BASE + 0x010))
#define GICC_RPR            (*(volatile u32 *)(GICC_BASE + 0x014))
#define GICC_HPPIR          (*(volatile u32 *)(GICC_BASE + 0x018))

static inline void dsb_sy(void){ __asm__ volatile("dsb sy" ::: "memory"); }
static inline void isb_sy(void){ __asm__ volatile("isb" ::: "memory"); }

/* ---------- Timers: CNTP (PPI 30) ---------- */
#define IRQ_CNTP       30U
#define INTID_SPURIOUS_MIN 1020U   /* 1020~1023: special/spurious IDs */

static inline u64 cntfrq_el0(void){ u64 v; __asm__("mrs %0, CNTFRQ_EL0":"=r"(v)); return v; }
static inline u64 cntp_ctl_el0_read(void){ u64 v; __asm__("mrs %0, CNTP_CTL_EL0":"=r"(v)); return v; }
static inline void cntp_ctl_el0_write(u64 v){ __asm__("msr CNTP_CTL_EL0, %0"::"r"(v)); isb_sy(); }
static inline void cntp_tval_el0_write(u64 v){ __asm__("msr CNTP_TVAL_EL0, %0"::"r"(v)); isb_sy(); }

static inline void cntp_disable(void){ u64 v=cntp_ctl_el0_read(); v&=~1ULL; cntp_ctl_el0_write(v); }
static inline void cntp_enable(void){  u64 v=cntp_ctl_el0_read(); v|= 1ULL; cntp_ctl_el0_write(v); }
static inline void cntp_mask(void){    u64 v=cntp_ctl_el0_read(); v|=(1ULL<<1); cntp_ctl_el0_write(v); }
static inline void cntp_unmask(void){  u64 v=cntp_ctl_el0_read(); v&=~(1ULL<<1); cntp_ctl_el0_write(v); }

/* ---------- 공용 컨텍스트 ---------- */
typedef struct { u64 x[31]; u64 elr_el1; u64 spsr_el1; } os_context_t;
extern os_context_t* os_schedule_from_isr(os_context_t *current);

/* ---------- 진단(원하면 주석 처리 가능) ---------- */
static inline char hexch(unsigned v){ v&=15; return v<10?('0'+v):('A'+(v-10)); }
static inline void uart_puthex32(u32 v){ uart_puts("0x"); for(int i=28;i>=0;i-=4) uart_putc(hexch((v>>i)&0xF)); }
static inline void uart_puthex64(u64 v){ uart_puts("0x"); for(int i=60;i>=0;i-=4) uart_putc(hexch((unsigned)(v>>i))); }
static inline u64 read_daif(void){ u64 v; __asm__("mrs %0, DAIF":"=r"(v)); return v; }
void diag_dump(const char* tag){
    uart_puts("\n[DIAG "); uart_puts(tag); uart_puts("]\n");
    uart_puts("GICD_CTLR="); uart_puthex32(GICD_CTLR);
    uart_puts("  IGROUPR0="); uart_puthex32(GICD_IGROUPR(0));
    uart_puts("  ISENABLER0="); uart_puthex32(GICD_ISENABLER(0));
    uart_puts("\nGICC_CTLR="); uart_puthex32(GICC_CTLR);
    uart_puts("  PMR="); uart_puthex32(GICC_PMR);
    uart_puts("  RPR="); uart_puthex32(GICC_RPR);
    uart_puts("  HPPIR="); uart_puthex32(GICC_HPPIR);
    uart_puts("\nCNTP_CTL_EL0="); uart_puthex64(cntp_ctl_el0_read());
    uart_puts("  CNTP_TVAL_EL0="); uart_puthex64(0); // 읽으면 카운터 변동 → 생략하거나 필요시 구현
    uart_puts("  CNTFRQ_EL0="); uart_puthex64(cntfrq_el0());
    uart_puts("  DAIF="); uart_puthex64(read_daif());
    uart_puts("\n");
}

/* ---------- GIC init ---------- */
void gic_init(void){
    dsb_sy();

    /* 1) Distributor: Group0/1 모두 enable */
    GICD_CTLR = (1U<<0) | (1U<<1);

    /* 2) CNTP(30)를 Group1(NS)로. 나머지는 손대지 않음 */
    u32 grp = GICD_IGROUPR(0);
    grp |= (1U << IRQ_CNTP);         /* CNTP -> Group1 */
    GICD_IGROUPR(0) = grp;

    /* 3) 모든 PPI(16~31)를 비활성/보류해제/Active 해제 */
    GICD_ICENABLER(0) = 0xFFFF0000U;
    GICD_ICPENDR(0)   = 0xFFFF0000U;
    GICD_ICACTIVER(0) = 0xFFFF0000U;

    /* 4) CNTP(30)만 Enable, 우선순위 설정 */
    GICD_ISENABLER(0) = (1U << IRQ_CNTP);
    GICD_IPRIORITYR(IRQ_CNTP) = 0x80;

    /* 5) CPU IF: PMR 허용, Group0/1 둘 다 enable(중요) */
    GICC_PMR  = 0xFF;
    GICC_BPR  = 0;                    /* optional */
    GICC_CTLR = (1U<<0) | (1U<<1);    /* EnableGrp0 | EnableGrp1 */

    dsb_sy(); isb_sy();
    diag_dump("after gic_init");
}

/* ---------- Timer init (100Hz) ---------- */
static inline u64 ticks_to_tval(u32 hz){
    u64 f = cntfrq_el0(); u64 t = f / (hz?hz:100);
    return t ? t : 1;
}
static inline void program_cntp(u32 hz){
    cntp_disable();
    cntp_tval_el0_write(ticks_to_tval(hz));
    cntp_unmask();
    cntp_enable();
}
void timer_init_100hz(void){
    cntp_mask();
    program_cntp(100);
    diag_dump("after timer_init_100hz");
}

/* ---------- IRQ handler ---------- */
os_context_t* irq_handler_c(os_context_t *tf){
    u32 iar   = GICC_IAR;
    u32 intid = iar & 0x3FFU;

    /* 스푸리어스(1020~1023)는 그냥 무시 (EOI 불필요) */
    if (intid >= INTID_SPURIOUS_MIN){
        return tf;
    }

    os_context_t *next_tf = tf;

    if (intid == IRQ_CNTP){
        /* CNTP 재무장 */
        cntp_mask();
        program_cntp(100);

        next_tf = os_schedule_from_isr(tf);

        dsb_sy();
        GICC_EOIR = iar;  /* 유효한 ID만 EOI */
        isb_sy();
        cntp_unmask();
        return next_tf;
    }

    /* 기타 인터럽트는 EOI만 */
    GICC_EOIR = iar;
    return next_tf;
}
