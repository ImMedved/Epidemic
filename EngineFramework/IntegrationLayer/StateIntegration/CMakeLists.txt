add_library(EpidemicGameFrameworkStateIntegration STATIC
    src/state_adapters.cpp
)

target_include_directories(EpidemicGameFrameworkStateIntegration PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkStateIntegration
    PUBLIC
        EpidemicFoundation
        EpidemicGameFrameworkEntities
        EpidemicGameFrameworkMaterials
        EpidemicGameFrameworkConditions
        EpidemicGameFrameworkEffects
        EpidemicGameplayQueries
        EpidemicGameplayFacts
        EpidemicGameplayTime
        EpidemicGameFrameworkCoreIntegration
)

epidemic_configure_target(EpidemicGameFrameworkStateIntegration)
