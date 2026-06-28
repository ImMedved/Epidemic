add_executable(EpidemicHeadlessCoreApp
    main.cpp
)

target_link_libraries(EpidemicHeadlessCoreApp
    PRIVATE EpidemicCore EpidemicDiagnostics
)

epidemic_configure_target(EpidemicHeadlessCoreApp)