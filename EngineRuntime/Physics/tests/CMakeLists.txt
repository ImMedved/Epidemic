add_executable(EpidemicRuntimePhysicsTests
    physics_tests.cpp
)

target_include_directories(EpidemicRuntimePhysicsTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(EpidemicRuntimePhysicsTests
    PRIVATE EpidemicRuntimePhysics
)

epidemic_configure_target(EpidemicRuntimePhysicsTests)
epidemic_add_test(EpidemicRuntimePhysicsTests)
