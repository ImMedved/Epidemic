add_library(EpidemicRuntimeStreaming STATIC
    src/streaming.cpp
    src/streaming_runtime_impl.cpp
)

target_include_directories(EpidemicRuntimeStreaming PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicRuntimeStreaming
    PUBLIC EpidemicRuntimeFoundation
    PRIVATE EpidemicFoundation
)

set(_epidemic_runtime_streaming_forbidden_targets
    EpidemicRuntimeRenderer
    EpidemicRuntimePhysics
    EpidemicRuntimeSupport
)

get_target_property(_epidemic_runtime_streaming_links EpidemicRuntimeStreaming LINK_LIBRARIES)
if(_epidemic_runtime_streaming_links STREQUAL "_epidemic_runtime_streaming_links-NOTFOUND")
    set(_epidemic_runtime_streaming_links "")
endif()

foreach(_forbidden_target IN LISTS _epidemic_runtime_streaming_forbidden_targets)
    if(_epidemic_runtime_streaming_links)
        list(FIND _epidemic_runtime_streaming_links "${_forbidden_target}" _forbidden_target_index)
        if(NOT _forbidden_target_index EQUAL -1)
            message(FATAL_ERROR "EpidemicRuntimeStreaming must not link runtime major '${_forbidden_target}'.")
        endif()
    endif()
endforeach()

epidemic_configure_target(EpidemicRuntimeStreaming)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
