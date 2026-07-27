cmake_minimum_required(VERSION 3.10)
project(qmgd C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(QMGD_DEBUG "Enable verbose stderr diagnostics in the decoder" OFF)

add_library(qmgd
    src/header.c
    src/decode_w2pass.c
    src/decode_a9ll.c
    src/decode.c
)
target_include_directories(qmgd PUBLIC include)
target_include_directories(qmgd PRIVATE src)

if(QMGD_DEBUG)
    target_compile_definitions(qmgd PRIVATE QMGD_DEBUG)
endif()

add_executable(qmgd_dump src/tools/qmgd_dump.c)
target_link_libraries(qmgd_dump qmgd)

install(TARGETS qmgd_dump RUNTIME DESTINATION bin)
