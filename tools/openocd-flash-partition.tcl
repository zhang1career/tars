# Partition-limited firmware flash for TARS (STM32F429).
# Erases flash bank0 sectors [first..last] only, then programs firmware.bin.
#
# Usage (from flash.sh):
#   openocd -f openocd.cfg \
#     -c "set FW_BIN {/path/to/tars.bin}" \
#     -c "set FW_BASE 0x08000000" \
#     -c "set FW_FIRST_SECTOR 0" \
#     -c "set FW_LAST_SECTOR 6" \
#     -c "source {/path/to/openocd-flash-partition.tcl}"

if {![info exists FW_BIN]} {
    echo "FW_BIN not set"
    shutdown error
}

if {![info exists FW_BASE]} {
    set FW_BASE 0x08000000
}

if {![info exists FW_FIRST_SECTOR]} {
    set FW_FIRST_SECTOR 0
}

if {![info exists FW_LAST_SECTOR]} {
    set FW_LAST_SECTOR 6
}

init
reset halt
flash protect 0 0 11 off
echo "==> erase bank0 sectors $FW_FIRST_SECTOR..$FW_LAST_SECTOR (preserve LittleFS+apps)"
flash erase_sector 0 $FW_FIRST_SECTOR $FW_LAST_SECTOR
reset halt
echo "==> program $FW_BIN at $FW_BASE"
flash write_image unlock $FW_BIN $FW_BASE
verify_image $FW_BIN $FW_BASE
echo "==> reset"
reset run
shutdown
