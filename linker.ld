/* linker.ld — AArch64 bare-metal for QEMU virt */

MEMORY {
  RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 128M
}

ENTRY(_start)

SECTIONS
{
  /* 코드/데이터 시작 */
  . = ORIGIN(RAM);

  /* [핵심 수정] 벡터 테이블을 가장 먼저, 2KiB 정렬하여 배치 (VBAR_EL1 요구사항) */
  .vectors : ALIGN(2048) {
    KEEP(*(.vectors))
  } > RAM

  /* 코드 섹션들 */
  .text : ALIGN(16) {
    KEEP(*(.text.boot))      /* entry.S의 _start */
    *(.text*)                /* 일반 코드 (interrupt_asm.S 포함) */
  } > RAM

  /* 읽기전용 데이터 */
  .rodata : ALIGN(16) {
    *(.rodata*)
  } > RAM

  /* 데이터 */
  .data : ALIGN(16) {
    *(.data*)
  } > RAM

  /* BSS */
  .bss : ALIGN(16) {
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    __bss_end = .;
  } > RAM

  /* 스택 포인터는 RAM의 가장 높은 주소를 가리키도록 설정 */
  . = ALIGN(16);
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
