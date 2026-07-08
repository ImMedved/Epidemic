add_executable(EpidemicRuntimeResourcesTests
    resources_tests.cpp
)

target_link_libraries(EpidemicRuntimeResourcesTests
    PRIVATE EpidemicRuntimeResources
)

epidemic_configure_target(EpidemicRuntimeResourcesTests)
add_test(NAME EpidemicRuntimeResourcesTests COMMAND EpidemicRuntimeResourcesTests)
