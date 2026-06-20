enable_language(C)

set(LUA51_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/lapi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/lcode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/ldebug.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/ldo.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/ldump.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/lfunc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/lgc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/llex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/lmem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/lobject.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/lopcodes.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/lparser.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/lstate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/lstring.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/ltable.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/ltm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/lundump.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/lvm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/lzio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/lauxlib.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/lbaselib.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/lmathlib.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/lstrlib.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/ltablib.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51/linit_tars.c
)

add_library(lua51 OBJECT ${LUA51_SRC})
target_include_directories(lua51 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Third_Party/lua51
)
target_compile_definitions(lua51 PUBLIC LUA_ANSI)
