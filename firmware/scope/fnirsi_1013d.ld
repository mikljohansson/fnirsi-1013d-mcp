MEMORY
{
	ram  : org = 0x7FFFFFE0, len = 32M
}

SECTIONS
{
  . = ORIGIN(ram);
  PRGM_START = .;
  .text : 
  {
    *start.o (.text)    /* was build/Debug/GNU_ARM-Linux/start.o (NetBeans object dir) */
    *(.text);
  } >ram

  .data :
  {
    *(.data);
    *(.rodata);
  } >ram
  PRGM_END = .;

  BSS_START = .;
  .bss : 
  {
    *(.bss);
  } >ram
  BSS_END = .;

  /* D4 logger (fw-d4): the 8 MiB DDR ring of the streaming logger (pc_hal_scope.c pc_log_mem, PC_LOG_RING_BYTES). NOLOAD:
     not in the image and not cleared by start.s (BSS_START .. BSS_END only), so neither the SD image nor the boot time
     grows; nothing reads it before a LOG_START resets the ring */
  .logring (NOLOAD) : ALIGN(4096)
  {
    LOGRING_START = .;
    *(.logring);
    LOGRING_END = .;
  } >ram
}

__program_size = PRGM_END - PRGM_START;

/* fw 0.2.48: the restart record's 1 MB section at 0x81A00000 is mapped strongly ordered (pc_mmu.c); nothing linked may
   reach it */
ASSERT(BSS_END <= 0x81A00000, "image + .bss reach the restart record section at 0x81A00000")
ASSERT(LOGRING_END <= 0x81A00000, "image + .bss + the logger's DDR ring reach the restart record section at 0x81A00000")
ASSERT(LOGRING_END - LOGRING_START >= 0x800000, "the logger's DDR ring is not in .logring")
