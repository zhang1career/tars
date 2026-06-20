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

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

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
