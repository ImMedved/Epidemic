add_library(EpidemicGameFrameworkProcessResourceSimulationIntegration STATIC
    src/process_resource_simulation_adapters.cpp
)

target_include_directories(EpidemicGameFrameworkProcessResourceSimulationIntegration PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkProcessResourceSimulationIntegration
    PUBLIC
        EpidemicGameFrameworkProcesses
        EpidemicGameFrameworkResourcesProduction
        EpidemicGameFrameworkSimulation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkProcessResourceSimulationIntegration)
