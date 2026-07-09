add_executable(EpidemicRuntimeSerializationTests
    serialization_tests.cpp
)

target_link_libraries(EpidemicRuntimeSerializationTests
    PRIVATE EpidemicRuntimeSerialization
)

target_include_directories(EpidemicRuntimeSerializationTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

epidemic_configure_target(EpidemicRuntimeSerializationTests)
add_test(NAME EpidemicRuntimeSerializationTests COMMAND EpidemicRuntimeSerializationTests)
