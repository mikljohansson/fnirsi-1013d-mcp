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
}

__program_size = PRGM_END - PRGM_START;

/* fw 0.2.48: the restart record's 1 MB section at 0x81A00000 is mapped strongly ordered (pc_mmu.c); nothing linked may
   reach it */
ASSERT(BSS_END <= 0x81A00000, "image + .bss reach the restart record section at 0x81A00000")
