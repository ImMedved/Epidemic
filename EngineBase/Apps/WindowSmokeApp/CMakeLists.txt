add_executable(EpidemicWindowSmokeApp
    main.cpp
)

target_link_libraries(EpidemicWindowSmokeApp
    PRIVATE EpidemicCore EpidemicPlatform
)

epidemic_configure_target(EpidemicWindowSmokeApp)