add_library(EpidemicGameFrameworkNarrativeIntegration STATIC
    src/narrative_adapters.cpp
)

target_include_directories(EpidemicGameFrameworkNarrativeIntegration PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkNarrativeIntegration
    PUBLIC
        EpidemicGameFrameworkNarrative
        EpidemicGameFrameworkKnowledge
        EpidemicGameFrameworkSaveGame
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkNarrativeIntegration)
