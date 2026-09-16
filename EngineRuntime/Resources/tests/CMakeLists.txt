add_executable(EpidemicRuntimeResourcesTests
    resources_tests.cpp
)

target_link_libraries(EpidemicRuntimeResourcesTests
    PRIVATE EpidemicRuntimeResources
)

target_include_directories(EpidemicRuntimeResourcesTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

epidemic_configure_target(EpidemicRuntimeResourcesTests)
epidemic_add_test(EpidemicRuntimeResourcesTests)
