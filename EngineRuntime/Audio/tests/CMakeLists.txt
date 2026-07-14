add_executable(EpidemicRuntimeAudioTests
    audio_tests.cpp
)

target_include_directories(EpidemicRuntimeAudioTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(EpidemicRuntimeAudioTests
    PRIVATE EpidemicRuntimeAudio
)

epidemic_configure_target(EpidemicRuntimeAudioTests)
add_test(NAME EpidemicRuntimeAudioTests COMMAND EpidemicRuntimeAudioTests)
