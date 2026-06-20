/*
 * Phase 2: relocatable native app linker script.
 * Linked at virtual base 0; runtime loader adds actual load address.
 */

OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(app_entry)

MEMORY
{
  APP_FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 48K
  APP_RAM  (rw) : ORIGIN = 0x20070000, LENGTH = 16K
}

SECTIONS
{
  .text :
  {
    KEEP(*(.text.app_entry))
    *(.text*)
    *(.rodata*)
  } > APP_FLASH

  .data :
  {
    _sidata = LOADADDR(.data);
    _sdata = .;
    *(.data*)
    _edata = .;
  } > APP_RAM AT > APP_FLASH

  .bss :
  {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    _ebss = .;
  } > APP_RAM

  /DISCARD/ :
  {
    *(.ARM.exidx*)
    *(.ARM.extab*)
  }
}
