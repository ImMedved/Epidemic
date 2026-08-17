add_library(EpidemicGameFrameworkTraversalNavigationConstructionIntegration STATIC
    src/traversal_navigation_construction_adapters.cpp
)

target_include_directories(EpidemicGameFrameworkTraversalNavigationConstructionIntegration PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkTraversalNavigationConstructionIntegration
    PUBLIC
        EpidemicGameFrameworkTraversal
        EpidemicGameFrameworkNavigationSemantics
        EpidemicGameFrameworkConstruction
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkTraversalNavigationConstructionIntegration)
