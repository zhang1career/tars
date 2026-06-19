set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

# Cross-compile checks must not link (avoids macOS linker flags with arm-none-eabi-ld)
set(CMAKE_TRY_COMPILE_TARGET_TYPE   STATIC_LIBRARY)

# Prefer ARM GNU Toolchain (includes newlib headers) over Homebrew's arm-none-eabi-gcc.
set(TOOLCHAIN_BIN_DIR "")
file(GLOB _ARM_TOOLCHAIN_GCCS "/Applications/ArmGNUToolchain/*/arm-none-eabi/bin/arm-none-eabi-gcc")
if(_ARM_TOOLCHAIN_GCCS)
    list(SORT _ARM_TOOLCHAIN_GCCS)
    list(REVERSE _ARM_TOOLCHAIN_GCCS)
    foreach(_gcc_candidate IN LISTS _ARM_TOOLCHAIN_GCCS)
        get_filename_component(_candidate_bin_dir "${_gcc_candidate}" DIRECTORY)
        get_filename_component(_candidate_root "${_candidate_bin_dir}" DIRECTORY)
        if(EXISTS "${_candidate_root}/arm-none-eabi/include/stdint.h")
            set(TOOLCHAIN_BIN_DIR "${_candidate_bin_dir}")
            break()
        endif()
    endforeach()
endif()

if(TOOLCHAIN_BIN_DIR)
    set(_TOOLCHAIN_C_COMPILER   "${TOOLCHAIN_BIN_DIR}/arm-none-eabi-gcc")
    set(_TOOLCHAIN_CXX_COMPILER "${TOOLCHAIN_BIN_DIR}/arm-none-eabi-c++")
else()
    find_program(_TOOLCHAIN_C_COMPILER   arm-none-eabi-gcc REQUIRED)
    find_program(_TOOLCHAIN_CXX_COMPILER arm-none-eabi-c++ REQUIRED)
    get_filename_component(TOOLCHAIN_BIN_DIR "${_TOOLCHAIN_C_COMPILER}" DIRECTORY)
endif()

# CLion's STM32 toolchain injects -DCMAKE_CXX_COMPILER=.../arm-none-eabi-c++.
# Use CACHE FORCE so a mid-configure cache reset does not fall back to host compilers.
set(CMAKE_C_COMPILER                ${_TOOLCHAIN_C_COMPILER} CACHE FILEPATH "" FORCE)
set(CMAKE_ASM_COMPILER              ${CMAKE_C_COMPILER} CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER              ${_TOOLCHAIN_CXX_COMPILER} CACHE FILEPATH "" FORCE)
set(CMAKE_LINKER                    "${TOOLCHAIN_BIN_DIR}/arm-none-eabi-g++")
set(CMAKE_OBJCOPY                   "${TOOLCHAIN_BIN_DIR}/arm-none-eabi-objcopy")
set(CMAKE_SIZE                      "${TOOLCHAIN_BIN_DIR}/arm-none-eabi-size")

set(CMAKE_EXECUTABLE_SUFFIX_ASM     ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C       ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX     ".elf")

# MCU specific flags
set(TARGET_FLAGS "-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard ")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${TARGET_FLAGS}")
set(CMAKE_ASM_FLAGS "${CMAKE_C_FLAGS} -x assembler-with-cpp -MMD -MP")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -fdata-sections -ffunction-sections -fstack-usage")

# The cyclomatic-complexity parameter must be defined for the Cyclomatic complexity feature in STM32CubeIDE to work.
# However, most GCC toolchains do not support this option, which causes a compilation error; for this reason, the feature is disabled by default.
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fcyclomatic-complexity")

set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_C_FLAGS_RELEASE "-Os -g0")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -g0")

set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fno-rtti -fno-exceptions -fno-threadsafe-statics")

set(CMAKE_EXE_LINKER_FLAGS "${TARGET_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -T \"${CMAKE_SOURCE_DIR}/STM32F429XX_FLASH.ld\"")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --specs=nano.specs")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-Map=${CMAKE_PROJECT_NAME}.map -Wl,--gc-sections")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--print-memory-usage")
set(TOOLCHAIN_LINK_LIBRARIES "m")
