add_library(EpidemicGameFrameworkSocialLegalIntegration STATIC
    src/social_legal_adapters.cpp
)

target_include_directories(EpidemicGameFrameworkSocialLegalIntegration PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkSocialLegalIntegration
    PUBLIC
        EpidemicGameFrameworkOwnership
        EpidemicGameFrameworkSociety
        EpidemicGameFrameworkCrime
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkSocialLegalIntegration)
