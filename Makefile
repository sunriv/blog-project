# Makefile — AArch64 bare-metal (QEMU virt + Cortex-A72) with QEMU log

# ---------- toolchain ----------
CROSS  ?= aarch64-none-elf-
CC      = $(CROSS)gcc
OBJDUMP = $(CROSS)objdump

# ---------- flags ----------
CFLAGS  = -mcpu=cortex-a72 -ffreestanding -fno-stack-protector -fno-builtin \
          -fno-pic -O2 -Wall -Wextra -g
ASFLAGS = $(CFLAGS)
LDFLAGS = -T linker.ld -nostdlib -static \
          -Wl,-Map=kernel.map -Wl,--gc-sections -Wl,--build-id=none

# ---------- sources ----------
SRCS_C = interrupt.c context.c main.c
# ctx_switch.S가 있으면 자동 포함, 없으면 무시되도록 wildcard 사용
SRCS_S = entry.S interrupt_asm.S $(wildcard ctx_switch.S)

OBJS   = $(SRCS_C:.c=.o) $(SRCS_S:.S=.o)

# ---------- qemu ----------
QEMU           ?= qemu-system-aarch64
QEMU_FLAGS = -machine virt,gic-version=2 -cpu cortex-a72 -smp 1 -m 128M \
             -serial stdio
QEMU_KERNEL    = -kernel kernel.elf
# 로그 마스크는 필요에 따라 바꿔도 됨: int,exec,in_asm,mmu,pcall,cpu 등
QEMU_LOG_MASK ?= int
QEMU_LOG_FILE ?= qemu.log

# ----- extra serial backends -----
UART_LOG_FILE ?= uart.log
# -serial stdio 를 제거한 공통 플래그 (다른 백엔드로 대체 시 사용)
QEMU_FLAGS_NOSTDIO := $(filter-out -serial stdio,$(QEMU_FLAGS))

# ---------- default ----------
all: kernel.elf

kernel.elf: $(OBJS) linker.ld
	$(CC) $(OBJS) $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(ASFLAGS) -c $< -o $@

# 기본 실행: 로그 파일 생성 (qemu.log)
run: kernel.elf
	$(QEMU) $(QEMU_FLAGS) $(QEMU_KERNEL) -d $(QEMU_LOG_MASK) -D $(QEMU_LOG_FILE)

# 로그 없이 실행하고 싶을 때
run-nolog: kernel.elf
	$(QEMU) $(QEMU_FLAGS) $(QEMU_KERNEL)

# PTY 모드: QEMU가 "char device redirected to /dev/pts/XX" 출력
run-pty: kernel.elf
	$(QEMU) $(QEMU_FLAGS_NOSTDIO) $(QEMU_KERNEL) \
		-chardev pty,id=char0 -serial chardev:char0 \
		-d $(QEMU_LOG_MASK) -D $(QEMU_LOG_FILE)

# 파일 로깅 모드: UART 출력이 $(UART_LOG_FILE) 로 저장
run-logfile: kernel.elf
	$(QEMU) $(QEMU_FLAGS_NOSTDIO) $(QEMU_KERNEL) \
		-serial file:$(UART_LOG_FILE) \
		-d $(QEMU_LOG_MASK) -D $(QEMU_LOG_FILE)

# 유용한 부가 타겟
disasm: kernel.elf
	$(OBJDUMP) -d kernel.elf > kernel.dis

clean:
	rm -f *.o *.elf *.map *.dis $(QEMU_LOG_FILE) $(UART_LOG_FILE)

# ----- TCP / Telnet / socket backends -----

# TCP raw serial: 게스트 UART를 localhost:7000에 바인딩 (raw는 기본값)
run-tcp: kernel.elf
	$(QEMU) $(QEMU_FLAGS_NOSTDIO) $(QEMU_KERNEL) \
		-serial tcp:127.0.0.1:7000,server,nowait \
		-d $(QEMU_LOG_MASK) -D $(QEMU_LOG_FILE)

# TCP telnet serial: telnet 클라이언트로 접속하고 싶을 때
run-telnet: kernel.elf
	$(QEMU) $(QEMU_FLAGS_NOSTDIO) $(QEMU_KERNEL) \
		-serial tcp:127.0.0.1:7000,server,nowait,telnet \
		-d $(QEMU_LOG_MASK) -D $(QEMU_LOG_FILE)

# -chardev socket 방식 (버전차 부작용 최소화)
run-socket: kernel.elf
	$(QEMU) $(QEMU_FLAGS_NOSTDIO) $(QEMU_KERNEL) \
		-chardev socket,id=char0,host=127.0.0.1,port=7000,server=on,wait=off \
		-serial chardev:char0 \
		-d $(QEMU_LOG_MASK) -D $(QEMU_LOG_FILE)

.PHONY: run-tcp run-telnet run-socket
