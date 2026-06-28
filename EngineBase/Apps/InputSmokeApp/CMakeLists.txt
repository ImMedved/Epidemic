add_executable(EpidemicInputSmokeApp
    main.cpp
)

target_link_libraries(EpidemicInputSmokeApp
    PRIVATE EpidemicCore EpidemicPlatform EpidemicInput
)

epidemic_configure_target(EpidemicInputSmokeApp)