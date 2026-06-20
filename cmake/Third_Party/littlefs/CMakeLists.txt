enable_language(C)

set(LITTLEFS_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/littlefs/lfs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/littlefs/lfs_util.c
)

add_library(littlefs OBJECT ${LITTLEFS_SRC})
target_include_directories(littlefs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/littlefs
)
target_compile_definitions(littlefs PUBLIC
    LFS_NO_MALLOC
    LFS_NO_DEBUG
    LFS_NO_WARN
    LFS_NO_ERROR
    LFS_NO_ASSERT
    LFS_THREADSAFE
)
