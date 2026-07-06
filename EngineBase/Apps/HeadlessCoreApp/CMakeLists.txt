add_executable(EpidemicHeadlessCoreApp
    main.cpp
)

target_link_libraries(EpidemicHeadlessCoreApp
    PRIVATE EpidemicAppSupport
)

epidemic_configure_target(EpidemicHeadlessCoreApp)