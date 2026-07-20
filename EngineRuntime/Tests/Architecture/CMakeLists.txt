add_executable(EpidemicRuntimeArchitectureTests
    architecture_tests.cpp
)

target_link_libraries(EpidemicRuntimeArchitectureTests
    PRIVATE EpidemicRuntimeFoundation
    PRIVATE EpidemicRuntimeResources
    PRIVATE EpidemicRuntimeAssets
    PRIVATE EpidemicRuntimeSerialization
    PRIVATE EpidemicRuntimePersistence
    PRIVATE EpidemicRuntimeTime
    PRIVATE EpidemicRuntimeEnvironment
    PRIVATE EpidemicRuntimeScene
    PRIVATE EpidemicRuntimeWorld
    PRIVATE EpidemicRuntimeStreaming
    PRIVATE EpidemicRuntimeRenderer
    PRIVATE EpidemicRuntimePhysics
    PRIVATE EpidemicRuntimeNavigation
    PRIVATE EpidemicRuntimeAnimation
    PRIVATE EpidemicRuntimeAudio
    PRIVATE EpidemicRuntimeSimulation
    PRIVATE EpidemicRuntimeSupport
)

epidemic_configure_target(EpidemicRuntimeArchitectureTests)
add_test(NAME EpidemicRuntimeArchitectureTests COMMAND EpidemicRuntimeArchitectureTests)
