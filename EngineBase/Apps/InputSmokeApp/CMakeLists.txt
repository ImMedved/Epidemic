add_executable(EpidemicInputSmokeApp
    main.cpp
)

target_link_libraries(EpidemicInputSmokeApp
    PRIVATE EpidemicAppSupport
)

epidemic_configure_target(EpidemicInputSmokeApp)