add_executable(EpidemicRuntimeAssetsTests
    assets_tests.cpp
)

target_link_libraries(EpidemicRuntimeAssetsTests
    PRIVATE EpidemicRuntimeAssets
)

epidemic_configure_target(EpidemicRuntimeAssetsTests)
add_test(NAME EpidemicRuntimeAssetsTests COMMAND EpidemicRuntimeAssetsTests)
