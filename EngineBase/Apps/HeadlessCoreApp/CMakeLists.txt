add_executable(EpidemicHeadlessCoreApp
    main.cpp
)

target_link_libraries(EpidemicHeadlessCoreApp
    PRIVATE EpidemicEngineBaseSupport
)

epidemic_configure_target(EpidemicHeadlessCoreApp)