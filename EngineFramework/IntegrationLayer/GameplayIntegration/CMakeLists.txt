add_library(EpidemicGameFrameworkGameplayIntegration STATIC
    src/gameplay_adapters.cpp
)

target_include_directories(EpidemicGameFrameworkGameplayIntegration PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkGameplayIntegration
    PUBLIC
        EpidemicGameFrameworkProgression
        EpidemicGameFrameworkCombat
        EpidemicGameFrameworkAbilities
        EpidemicGameFrameworkLoot
        EpidemicGameFrameworkEffects
        EpidemicGameFrameworkConditions
        EpidemicGameplayTime
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkGameplayIntegration)
