add_library(EpidemicGameFrameworkPopulationSimulationIntegration STATIC
    src/population_simulation_adapters.cpp
)

target_include_directories(EpidemicGameFrameworkPopulationSimulationIntegration PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkPopulationSimulationIntegration
    PUBLIC
        EpidemicGameFrameworkPopulation
        EpidemicGameFrameworkEncounters
        EpidemicGameFrameworkRolesJobs
        EpidemicGameFrameworkNeedsLife
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkPopulationSimulationIntegration)
