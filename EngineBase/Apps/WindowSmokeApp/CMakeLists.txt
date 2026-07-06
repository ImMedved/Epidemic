add_executable(EpidemicWindowSmokeApp
    main.cpp
)

target_link_libraries(EpidemicWindowSmokeApp
    PRIVATE EpidemicEngineBaseSupport
)

epidemic_configure_target(EpidemicWindowSmokeApp)