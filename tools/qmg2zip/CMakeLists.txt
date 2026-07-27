add_executable(qmg2zip
    main.cpp
)
target_link_libraries(qmg2zip PRIVATE bootanim_common qmgd pthread)

install(TARGETS qmg2zip RUNTIME DESTINATION bin)
