add_executable(zip2qmg
    main.cpp
)
target_link_libraries(zip2qmg PRIVATE bootanim_common qmgc_lib pthread)

install(TARGETS zip2qmg RUNTIME DESTINATION bin)
