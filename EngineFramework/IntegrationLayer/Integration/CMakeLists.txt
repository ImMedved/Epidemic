add_library(EpidemicGameFrameworkCoreIntegration STATIC
    src/core_adapters.cpp
)

target_include_directories(EpidemicGameFrameworkCoreIntegration PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkCoreIntegration
    PUBLIC
        EpidemicGameplayQueries
        EpidemicGameplayFacts
        EpidemicGameplayTime
        EpidemicGameFrameworkSaveGame
        EpidemicRuntimeTime
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkCoreIntegration)
