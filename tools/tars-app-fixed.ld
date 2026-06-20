/*
 * Phase 1: fixed-slot native app linker script.
 * Usage: arm-none-eabi-gcc ... -T tars-app-fixed.ld -Wl,--defsym=TARS_SLOT_INDEX=0
 */

OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(app_entry)

__slot_index = DEFINED(TARS_SLOT_INDEX) ? TARS_SLOT_INDEX : 0;
__slot_stride = 0x20000;
__link_base = 0x080A0000 + (__slot_index * __slot_stride);

MEMORY
{
  APP_FLASH (rx) : ORIGIN = __link_base, LENGTH = 48K
  APP_RAM  (rw) : ORIGIN = 0x20070000, LENGTH = 16K
}

_estack = ORIGIN(APP_RAM) + LENGTH(APP_RAM);

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
}
