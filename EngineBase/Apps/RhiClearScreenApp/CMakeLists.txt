add_executable(EpidemicRhiClearScreenApp
    main.cpp
)

target_link_libraries(EpidemicRhiClearScreenApp
    PRIVATE EpidemicEngineBaseSupport
)

epidemic_configure_target(EpidemicRhiClearScreenApp)