add_executable(EpidemicInputSmokeApp
    main.cpp
)

target_link_libraries(EpidemicInputSmokeApp
    PRIVATE EpidemicEngineBaseSupport
)

epidemic_configure_target(EpidemicInputSmokeApp)