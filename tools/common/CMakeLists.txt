find_package(PNG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET libzip)

add_library(bootanim_common STATIC
    desc.cpp
    bootanim_zip.cpp
)
target_include_directories(bootanim_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bootanim_common PUBLIC qmgc_lib PNG::PNG PkgConfig::LIBZIP)
