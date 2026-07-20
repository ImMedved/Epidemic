add_library(EpidemicRuntimeAudio STATIC
    src/audio.cpp
    src/audio_runtime_impl.cpp
)

target_include_directories(EpidemicRuntimeAudio PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicRuntimeAudio
    PUBLIC EpidemicRuntimeFoundation
    PRIVATE EpidemicFoundation
)

set(_epidemic_runtime_audio_forbidden_targets
    EpidemicRuntimeScene
    EpidemicRuntimeRenderer
    EpidemicRuntimeWorld
    EpidemicRuntimeSimulation
    EpidemicRuntimeSupport
)

get_target_property(_epidemic_runtime_audio_links EpidemicRuntimeAudio LINK_LIBRARIES)
if(_epidemic_runtime_audio_links STREQUAL "_epidemic_runtime_audio_links-NOTFOUND")
    set(_epidemic_runtime_audio_links "")
endif()

foreach(_forbidden_target IN LISTS _epidemic_runtime_audio_forbidden_targets)
    if(_epidemic_runtime_audio_links)
        list(FIND _epidemic_runtime_audio_links "${_forbidden_target}" _forbidden_target_index)
        if(NOT _forbidden_target_index EQUAL -1)
            message(FATAL_ERROR "EpidemicRuntimeAudio must not link runtime major '${_forbidden_target}'.")
        endif()
    endif()
endforeach()

epidemic_configure_target(EpidemicRuntimeAudio)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
