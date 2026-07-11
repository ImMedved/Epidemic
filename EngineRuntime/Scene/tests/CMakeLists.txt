add_executable(EpidemicRuntimeSceneTests
    scene_tests.cpp
)

target_link_libraries(EpidemicRuntimeSceneTests
    PRIVATE EpidemicRuntimeScene
)

target_include_directories(EpidemicRuntimeSceneTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

epidemic_configure_target(EpidemicRuntimeSceneTests)
add_test(NAME EpidemicRuntimeSceneTests COMMAND EpidemicRuntimeSceneTests)
