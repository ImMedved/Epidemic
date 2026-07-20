add_executable(EpidemicRuntimePersistenceTests
    persistence_tests.cpp
)

target_link_libraries(EpidemicRuntimePersistenceTests
    PRIVATE EpidemicRuntimePersistence
)

target_include_directories(EpidemicRuntimePersistenceTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

epidemic_configure_target(EpidemicRuntimePersistenceTests)
add_test(NAME EpidemicRuntimePersistenceTests COMMAND EpidemicRuntimePersistenceTests)
