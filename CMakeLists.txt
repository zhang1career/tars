cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME tars)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Cross-compile: compiler sanity checks must not link on the host
if(CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
endif()

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)
add_subdirectory(cmake/Third_Party/lua51)
add_subdirectory(cmake/Third_Party/littlefs)

# Board pin map: CSV -> generated C (see tools/pinmap/)
set(TARS_BOARD_ID "stm32f429i-disc1")
set(TARS_PINMAP_CSV "${CMAKE_SOURCE_DIR}/tools/pinmap/${TARS_BOARD_ID}.csv")
set(TARS_PINMAP_GEN_DIR "${CMAKE_SOURCE_DIR}/generated/pinmap")
set(TARS_PINMAP_GEN_C "${TARS_PINMAP_GEN_DIR}/${TARS_BOARD_ID}.c")

add_custom_command(
    OUTPUT ${TARS_PINMAP_GEN_C}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${TARS_PINMAP_GEN_DIR}
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/pinmap/pinmap-gen.py
            ${TARS_PINMAP_CSV} -o ${TARS_PINMAP_GEN_C}
    DEPENDS ${TARS_PINMAP_CSV} ${CMAKE_SOURCE_DIR}/tools/pinmap/pinmap-gen.py
    COMMENT "Generating pin map ${TARS_BOARD_ID}.c"
    VERBATIM
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${TARS_PINMAP_GEN_C})
set_source_files_properties(${TARS_PINMAP_GEN_C} PROPERTIES GENERATED TRUE)

# Probe metrics: CSV -> generated C (see tools/probe/)
set(TARS_PROBE_CSV "${CMAKE_SOURCE_DIR}/tools/probe/metrics.csv")
set(TARS_PROBE_GEN_DIR "${CMAKE_SOURCE_DIR}/generated/probe")
set(TARS_PROBE_GEN_C "${TARS_PROBE_GEN_DIR}/metrics.c")

add_custom_command(
    OUTPUT ${TARS_PROBE_GEN_C}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${TARS_PROBE_GEN_DIR}
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/probe/probe-gen.py
            ${TARS_PROBE_CSV} -o ${TARS_PROBE_GEN_C}
    DEPENDS ${TARS_PROBE_CSV} ${CMAKE_SOURCE_DIR}/tools/probe/probe-gen.py
    COMMENT "Generating probe metrics metrics.c"
    VERBATIM
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${TARS_PROBE_GEN_C})
set_source_files_properties(${TARS_PROBE_GEN_C} PROPERTIES GENERATED TRUE)

# FOC controller: Simulink / Embedded-Coder output synced from motor-ctrl-sim.
# The algorithm's source of truth is that project's codegen_stm32/; it is
# copied here verbatim (never edited) by tools/foc/foc-sync.py, mirroring the
# pin map / probe code-gen flow above.  Override the source checkout with
#   -DTARS_FOC_CODEGEN_DIR=/path/to/motor-ctrl-sim/codegen_stm32
# (otherwise the script falls back to $TARS_FOC_CODEGEN_DIR or the sibling repo).
set(TARS_FOC_GEN_DIR "${CMAKE_SOURCE_DIR}/generated/foc")
set(TARS_FOC_CODEGEN_DIR "" CACHE PATH "Path to motor-ctrl-sim/codegen_stm32 (FOC source of truth)")
set(TARS_FOC_GEN_SRCS
    ${TARS_FOC_GEN_DIR}/foc_step_stm32.c
    ${TARS_FOC_GEN_DIR}/foc_step_stm32_data.c
    ${TARS_FOC_GEN_DIR}/foc_step_stm32_initialize.c
    ${TARS_FOC_GEN_DIR}/foc_step_stm32_terminate.c
)
set(TARS_FOC_GEN_PARAMS ${TARS_FOC_GEN_DIR}/foc_params.h)

if(TARS_FOC_CODEGEN_DIR)
    set(TARS_FOC_SRC_ARG --src ${TARS_FOC_CODEGEN_DIR})
else()
    set(TARS_FOC_SRC_ARG "")
endif()

add_custom_command(
    OUTPUT ${TARS_FOC_GEN_SRCS} ${TARS_FOC_GEN_PARAMS}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${TARS_FOC_GEN_DIR}
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/foc/foc-sync.py ${TARS_FOC_SRC_ARG}
            -o ${TARS_FOC_GEN_DIR}
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/foc/foc-sync.py
    COMMENT "Syncing FOC controller sources + foc_params.h into generated/foc"
    VERBATIM
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${TARS_FOC_GEN_SRCS})
set_source_files_properties(${TARS_FOC_GEN_SRCS} PROPERTIES GENERATED TRUE)
set_source_files_properties(${TARS_FOC_GEN_PARAMS} PROPERTIES GENERATED TRUE)

# foc_params.h is included by Core/Src/tim.c and App/foc/tars_foc.c, which are
# not in the custom_command's output deps -- gate the whole executable on the
# sync so the header always exists before any source compiles.
add_custom_target(tars_foc_sync DEPENDS ${TARS_FOC_GEN_SRCS} ${TARS_FOC_GEN_PARAMS})
add_dependencies(${CMAKE_PROJECT_NAME} tars_foc_sync)

# Treat the generated FOC tree as a system include so its (academic-license,
# Embedded-Coder) headers don't trip the firmware's stricter warnings.
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE ${TARS_FOC_GEN_DIR})
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/App/foc)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# User application sources.
#
# NOTE: these intentionally live here (the user-owned root CMakeLists, which
# STM32CubeMX generates ONCE and never overwrites) and NOT in
# cmake/stm32cubemx/CMakeLists.txt, which CubeMX fully regenerates on every
# `project generate`. Keeping them here makes the .ioc -> regen workflow safe.
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # App layer
    ${CMAKE_SOURCE_DIR}/App/usb_otg.c
    ${CMAKE_SOURCE_DIR}/App/shell.c
    ${CMAKE_SOURCE_DIR}/App/tars_api.c
    ${CMAKE_SOURCE_DIR}/App/mcu/tars_mcu_gpio.c
    ${CMAKE_SOURCE_DIR}/App/mcu/tars_mcu_pwm.c
    ${CMAKE_SOURCE_DIR}/App/mcu/tars_mcu_shell.c
    ${CMAKE_SOURCE_DIR}/App/mcu/tars_mcu_pinmap.c
    ${CMAKE_SOURCE_DIR}/App/resource/tars_resource.c
    ${CMAKE_SOURCE_DIR}/App/resource/tars_res_gpio.c
    ${CMAKE_SOURCE_DIR}/App/resource/tars_res_mgr.c
    ${CMAKE_SOURCE_DIR}/App/resource/tars_res_pwm.c
    ${CMAKE_SOURCE_DIR}/App/probe/tars_probe.c
    ${CMAKE_SOURCE_DIR}/App/probe/tars_probe_uart.c
    ${CMAKE_SOURCE_DIR}/App/probe/tars_probe_capture.c
    ${CMAKE_SOURCE_DIR}/App/probe/tars_probe_metrics.c
    ${CMAKE_SOURCE_DIR}/App/tars_vfs.c
    ${CMAKE_SOURCE_DIR}/App/tars_crc.c
    ${CMAKE_SOURCE_DIR}/App/tars_loader.c
    ${CMAKE_SOURCE_DIR}/App/tars_manifest.c
    ${CMAKE_SOURCE_DIR}/App/tars_registry.c
    ${CMAKE_SOURCE_DIR}/App/tars_storage.c
    ${CMAKE_SOURCE_DIR}/App/tars_flash_hw.c
    ${CMAKE_SOURCE_DIR}/App/tars_lfs.c
    ${CMAKE_SOURCE_DIR}/App/tars_lua.c
    ${CMAKE_SOURCE_DIR}/App/tars_ota.c
    ${CMAKE_SOURCE_DIR}/App/tars_hal.c
    ${CMAKE_SOURCE_DIR}/App/tars_motor.c
    ${CMAKE_SOURCE_DIR}/App/foc/tars_foc.c
    ${CMAKE_SOURCE_DIR}/App/tars_builtin_apps.c
    ${CMAKE_SOURCE_DIR}/App/apps/app_motor_ctl.c
    ${CMAKE_SOURCE_DIR}/App/tars_sys.c
    # LCD entry-point stubs (active only when TARS_FEATURE_LCD=0)
    ${CMAKE_SOURCE_DIR}/App/lcd/lcd_stubs.c
    # USB Device (CDC) — not described by the .ioc, so kept here
    ${CMAKE_SOURCE_DIR}/USB_DEVICE/App/usb_device.c
    ${CMAKE_SOURCE_DIR}/USB_DEVICE/Target/usbd_conf.c
    ${CMAKE_SOURCE_DIR}/USB_DEVICE/Target/usbd_desc.c
    ${CMAKE_SOURCE_DIR}/USB_DEVICE/Target/usbd_cdc_if.c
    ${CMAKE_SOURCE_DIR}/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c
    ${CMAKE_SOURCE_DIR}/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c
    ${CMAKE_SOURCE_DIR}/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c
    ${CMAKE_SOURCE_DIR}/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Src/usbd_cdc.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pcd.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pcd_ex.c
    # ADC1 injected sensing for FOC (hand-authored; not in the CubeMX baseline)
    ${CMAKE_SOURCE_DIR}/Core/Src/adc.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc_ex.c
)

# User include paths (see note above — owned by this file, survives regen).
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/App
    ${CMAKE_SOURCE_DIR}/App/lcd
    ${CMAKE_SOURCE_DIR}/App/mcu
    ${CMAKE_SOURCE_DIR}/App/resource
    ${CMAKE_SOURCE_DIR}/App/probe
    ${CMAKE_SOURCE_DIR}/App/foc
    ${CMAKE_SOURCE_DIR}/Utilities/Fonts
    ${CMAKE_SOURCE_DIR}/Third_Party/lua51
    ${CMAKE_SOURCE_DIR}/Third_Party/littlefs
    ${CMAKE_SOURCE_DIR}/USB_DEVICE/App
    ${CMAKE_SOURCE_DIR}/USB_DEVICE/Target
    ${CMAKE_SOURCE_DIR}/Middlewares/ST/STM32_USB_Device_Library/Core/Inc
    ${CMAKE_SOURCE_DIR}/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc
)

# LCD feature switch. OFF = motor-control variant (DISC1): the LCD stack is not
# compiled and its pins (LTDC/SPI5/I2C3) are freed for TIM1 PWM. The LCD code is
# NOT deleted — flip this option to build the display variant. The same switch
# drives the TARS_FEATURE_LCD macro used by the C call sites (see App/tars_features.h).
option(TARS_ENABLE_LCD "Build the LCD/LTDC display stack (off = motor variant)" OFF)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TARS_FEATURE_LCD=$<BOOL:${TARS_ENABLE_LCD}>)

if(TARS_ENABLE_LCD)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/App/lcd/lcd_io.c
        ${CMAKE_SOURCE_DIR}/App/lcd/lcd_fb.c
        ${CMAKE_SOURCE_DIR}/App/lcd/lcd_text.c
        ${CMAKE_SOURCE_DIR}/App/lcd/lcd_log.c
        ${CMAKE_SOURCE_DIR}/App/lcd/lcd_viewport.c
        ${CMAKE_SOURCE_DIR}/App/lcd/ili9341.c
        ${CMAKE_SOURCE_DIR}/App/apps/app_viewport.c
        ${CMAKE_SOURCE_DIR}/Utilities/Fonts/font8.c
    )
endif()

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    lua51
    littlefs

    # Add user defined libraries
)
