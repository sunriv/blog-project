// interrupt.c — AArch64 bare-metal (QEMU virt, GICv2)
// CNTP(Non-secure Physical Timer, PPI 30)만 사용.
// Secure EL1이면 PPI30을 Group0로 라우팅하여 Secure CPU IF가 직접 받도록 설정.
// ISR은 오직 플래그만 세우고 SEV로 메인을 깨운다(출력 금지).

#include <stdint.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t  u8;

/* ---------- UART (진단용만) ---------- */
#define UART0_BASE   0x09000000UL
#define UART_DR      (*(volatile u32 *)(UART0_BASE + 0x00))
#define UART_FR      (*(volatile u32 *)(UART0_BASE + 0x18))
#define UART_FR_TXFF (1u << 5)
static inline void uart_putc(char c){ while (UART_FR & UART_FR_TXFF){} UART_DR = (u32)c; }
static inline void uart_puts(const char*s){ while(*s) uart_putc(*s++); }
static inline char hexch(unsigned v){ v&=15; return v<10?('0'+v):('A'+(v-10)); }
static inline void uart_puthex32(u32 v){ uart_puts("0x"); for(int i=28;i>=0;i-=4) uart_putc(hexch((v>>i)&0xF)); }
static inline void uart_puthex64(u64 v){ uart_puts("0x"); for(int i=60;i>=0;i-=4) uart_putc(hexch((unsigned)(v>>i))); }

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
#define GICD_ICFGR(n)       (*(volatile u32 *)(GICD_BASE + 0xC00 + 4*(n)))

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
static inline void dmb_ish(void){ __asm__ volatile("dmb ish" ::: "memory"); }
static inline void sev(void){ __asm__ volatile("sev" ::: "memory"); }

/* ---------- Generic Timer: CNTP (PPI 30) ---------- */
#define IRQ_CNTP 30U

static inline u64 cntfrq_el0(void){ u64 v; __asm__("mrs %0, CNTFRQ_EL0":"=r"(v)); return v; }
static inline u64 cntpct_el0(void){ u64 v; __asm__("mrs %0, CNTPCT_EL0":"=r"(v)); return v; }

static inline u64 cnTP_ctl_read(void){ u64 v; __asm__("mrs %0, CNTP_CTL_EL0":"=r"(v)); return v; }
static inline void cnTP_ctl_write(u64 v){ __asm__("msr CNTP_CTL_EL0, %0"::"r"(v)); isb_sy(); }
static inline u64 cnTP_tval_read(void){ u64 v; __asm__("mrs %0, CNTP_TVAL_EL0":"=r"(v)); return v; }
static inline void cnTP_tval_write(u64 v){ __asm__("msr CNTP_TVAL_EL0, %0"::"r"(v)); isb_sy(); }

/* ---------- 공용 컨텍스트 ---------- */
typedef struct { u64 x[31]; u64 elr_el1; u64 spsr_el1; } os_context_t;
extern os_context_t* os_schedule_from_isr(os_context_t *current); // 사용 안 함(현재 프레임 유지)

/* ---------- 메인과 공유하는 플래그 ---------- */
extern volatile int g_tick_req;
extern volatile int g_tick_ack;

/* ---------- 런타임 상태 ---------- */
static int g_secure_mode = 0;  /* 1=Secure EL1 */
static const u32 g_timer_irq = IRQ_CNTP;

/* ---------- 도우미 출력 ---------- */
static inline u64 read_daif(void){ u64 v; __asm__("mrs %0, DAIF":"=r"(v)); return v; }
static inline u64 read_currentel(void){ u64 v; __asm__("mrs %0, CurrentEL":"=r"(v)); return v; }

void dump_short(const char* tag){
    uart_puts("\n[DIAG "); uart_puts(tag); uart_puts("]\n");
    uart_puts("mode="); uart_puts(g_secure_mode?"SECURE":"NONSEC");
    uart_puts("  irq="); uart_puthex32(g_timer_irq);
    uart_puts("  EL="); uart_puthex64(read_currentel());
    uart_puts("  DAIF="); uart_puthex64(read_daif());
    uart_puts("\nGICD_CTLR="); uart_puthex32(GICD_CTLR);
    uart_puts(" IGROUPR0="); uart_puthex32(GICD_IGROUPR(0));
    uart_puts(" ISENABLER0="); uart_puthex32(GICD_ISENABLER(0));
    uart_puts("\nGICC_CTLR="); uart_puthex32(GICC_CTLR);
    uart_puts(" PMR="); uart_puthex32(GICC_PMR);
    uart_puts(" RPR="); uart_puthex32(GICC_RPR);
    uart_puts(" HPPIR="); uart_puthex32(GICC_HPPIR);
    uart_puts("\nCNTP_CTL_EL0="); uart_puthex64(cnTP_ctl_read());
    uart_puts("  CNTP_TVAL_EL0="); uart_puthex64(cnTP_tval_read());
    uart_puts("  CNTPCT_EL0="); uart_puthex64(cntpct_el0());
    uart_puts("\n");
}

/* ---------- Secure/Non-secure 감지 ---------- */
static int detect_secure_mode(void){
    u32 orig = GICD_IGROUPR(0);
    u32 set1 = orig | (1U<<30);
    GICD_IGROUPR(0) = set1; dsb_sy();
    u32 r1 = GICD_IGROUPR(0);
    u32 clr = set1 & ~(1U<<30);
    GICD_IGROUPR(0) = clr; dsb_sy();
    u32 r2 = GICD_IGROUPR(0);
    return ((r1 & (1U<<30)) && !(r2 & (1U<<30))) ? 1 : 0;
}

/* ---------- GIC init ---------- */
void gic_init(void){
    dsb_sy();

    /* Distributor: Group0/1 enable */
    GICD_CTLR = (1U<<0) | (1U<<1);

    g_secure_mode = detect_secure_mode();

    /* 모든 PPI(16..31) disable/pending/active clear */
    GICD_ICENABLER(0) = 0xFFFF0000U;
    GICD_ICPENDR(0)   = 0xFFFF0000U;
    GICD_ICACTIVER(0) = 0xFFFF0000U;

    /* CNTP(30) 라우팅: Secure면 Group0(=0), Non-secure면 Group1(=1) */
    u32 grp = GICD_IGROUPR(0);
    if (g_secure_mode) grp &= ~(1U<<30); else grp |= (1U<<30);
    GICD_IGROUPR(0) = grp;

    /* 레벨 트리거(00) 고정 */
    u32 icfgr1 = GICD_ICFGR(1);
    icfgr1 &= ~(3u << ((30-16)*2));
    GICD_ICFGR(1) = icfgr1;

    /* CNTP Enable + priority */
    GICD_ISENABLER(0) = (1U<<30);
    GICD_IPRIORITYR(30) = 0x80;

    /* CPU IF: PMR=all, Enable G0/G1, FIQEn(Grp0→FIQ) */
    GICC_PMR  = 0xFF;
    GICC_BPR  = 0;
    GICC_CTLR = (1U<<0) | (1U<<1) | (1U<<3);

    dsb_sy(); isb_sy();
    dump_short("after gic_init");
}

/* ---------- Timer (100Hz) ---------- */
static inline u64 ticks_to_tval(u32 hz){
    u64 f = cntfrq_el0(); u64 t = f / (hz?hz:100);
    return t ? t : 1;
}
static inline void timer_disable(void){ u64 v=cnTP_ctl_read(); v&=~1ULL; cnTP_ctl_write(v); }
static inline void timer_enable(void){  u64 v=cnTP_ctl_read(); v|= 1ULL; cnTP_ctl_write(v); }
static inline void timer_mask(void){    u64 v=cnTP_ctl_read(); v|=(1ULL<<1); cnTP_ctl_write(v); }
static inline void timer_unmask(void){  u64 v=cnTP_ctl_read(); v&=~(1ULL<<1); cnTP_ctl_write(v); }
static inline void program_timer(u32 hz){
    timer_disable();
    cnTP_tval_write(ticks_to_tval(hz));
    timer_unmask();
    timer_enable();
}
void timer_init_100hz(void){
    timer_mask();
    program_timer(100);
    dump_short("after timer_init_100hz");
}

/* ---------- IRQ/FIQ 공용 핸들러 ---------- */
os_context_t* irq_handler_c(os_context_t *tf){
    u32 iar   = GICC_IAR;
    u32 intid = iar & 0x3FFU;

    /* 1020~1023: spurious/special → 무시 */
    if (intid >= 1020U){
        return tf;
    }

    if (intid == IRQ_CNTP){
        /* 타이머 재무장 */
        timer_mask();
        program_timer(100);

        /* 요청 있으면: ISR은 요청을 '접수'만 하고 메인으로 알림 */
        if (g_tick_req){
            dmb_ish();
            g_tick_req = 0;
            g_tick_ack = 1;
            dmb_ish();
            sev();          // 메인의 WFE 깨우기
        }

        dsb_sy();
        GICC_EOIR = iar;
        isb_sy();
        timer_unmask();
        return tf;          // 컨텍스트는 유지
    }

    /* 기타 인터럽트 */
    GICC_EOIR = iar;
    return tf;
}
