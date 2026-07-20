add_executable(EpidemicRuntimeAssetsTests
    assets_tests.cpp
)

target_include_directories(EpidemicRuntimeAssetsTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(EpidemicRuntimeAssetsTests
    PRIVATE EpidemicRuntimeAssets
)

epidemic_configure_target(EpidemicRuntimeAssetsTests)
add_test(NAME EpidemicRuntimeAssetsTests COMMAND EpidemicRuntimeAssetsTests)
