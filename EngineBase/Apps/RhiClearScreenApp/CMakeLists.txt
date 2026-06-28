add_executable(EpidemicRhiClearScreenApp
    main.cpp
)

target_link_libraries(EpidemicRhiClearScreenApp
    PRIVATE EpidemicCore EpidemicPlatform EpidemicInput EpidemicRHI EpidemicRHI_D3D11
)

epidemic_configure_target(EpidemicRhiClearScreenApp)