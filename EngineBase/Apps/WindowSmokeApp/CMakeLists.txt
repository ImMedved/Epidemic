add_executable(EpidemicWindowSmokeApp
    main.cpp
)

target_link_libraries(EpidemicWindowSmokeApp
    PRIVATE EpidemicAppSupport
)

epidemic_configure_target(EpidemicWindowSmokeApp)