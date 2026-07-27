find_package(PNG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET libzip)

add_executable(bootanim_qmg
    main.cpp
    desc.cpp
    bootanim_zip.cpp
)
target_link_libraries(bootanim_qmg PRIVATE qmgc_lib qmgd pthread PNG::PNG PkgConfig::LIBZIP)

install(TARGETS bootanim_qmg RUNTIME DESTINATION bin)
