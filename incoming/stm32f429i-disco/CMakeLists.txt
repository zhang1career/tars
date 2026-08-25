cmake_minimum_required(VERSION 3.22)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

find_program(INCOMING_CC arm-none-eabi-gcc REQUIRED)
find_program(INCOMING_OBJCOPY arm-none-eabi-objcopy REQUIRED)
find_program(INCOMING_SIZE arm-none-eabi-size REQUIRED)

set(CMAKE_C_COMPILER ${INCOMING_CC})
set(CMAKE_ASM_COMPILER ${INCOMING_CC})

set(TARS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

project(incoming-stm32f429i-disco C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(TARGET_FLAGS -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard)

add_executable(${PROJECT_NAME}
    Src/main.c
    Src/incoming_clock.c
    Src/incoming_tests.c
    Src/lcd_io.c
    Src/stm32f4xx_it.c
    ${TARS_ROOT}/App/lcd/ili9341.c
    ${TARS_ROOT}/Core/Src/system_stm32f4xx.c
    ${TARS_ROOT}/Core/Src/sysmem.c
    ${TARS_ROOT}/startup_stm32f429xx.s
    ${TARS_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c
    ${TARS_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c
    ${TARS_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c
    ${TARS_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c
    ${TARS_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c
    ${TARS_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c
    ${TARS_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma_ex.c
    ${TARS_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c
    ${TARS_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c
    ${TARS_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c
    ${TARS_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ex.c
    ${TARS_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_exti.c
    ${TARS_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_fmc.c
    ${TARS_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sdram.c
    ${TARS_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_spi.c
    ${TARS_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c.c
    ${TARS_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c_ex.c
    ${TARS_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_ltdc.c
    ${TARS_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_ltdc_ex.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
    Inc
    ${TARS_ROOT}/App/lcd
    ${TARS_ROOT}/Core/Inc
    ${TARS_ROOT}/Drivers/STM32F4xx_HAL_Driver/Inc
    ${TARS_ROOT}/Drivers/STM32F4xx_HAL_Driver/Inc/Legacy
    ${TARS_ROOT}/Drivers/CMSIS/Device/ST/STM32F4xx/Include
    ${TARS_ROOT}/Drivers/CMSIS/Include
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    USE_HAL_DRIVER
    STM32F429xx
)

target_compile_options(${PROJECT_NAME} PRIVATE
    ${TARGET_FLAGS}
    -Wall
    -Wextra
    -fdata-sections
    -ffunction-sections
    $<$<CONFIG:Debug>:-O0 -g3>
    $<$<CONFIG:Release>:-Os -g0>
)

target_link_options(${PROJECT_NAME} PRIVATE
    ${TARGET_FLAGS}
    -T${TARS_ROOT}/STM32F429XX_FLASH.ld
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.map
    -Wl,--print-memory-usage
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    SUFFIX ".elf"
    ADDITIONAL_CLEAN_FILES ${PROJECT_NAME}.map
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${INCOMING_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.bin
    COMMAND ${INCOMING_SIZE} $<TARGET_FILE:${PROJECT_NAME}>
    COMMENT "incoming: bin + size"
)
