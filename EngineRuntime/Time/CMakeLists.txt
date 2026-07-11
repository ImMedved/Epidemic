add_library(EpidemicRuntimeTime STATIC
    src/time.cpp
    src/time_runtime_impl.cpp
)

target_include_directories(EpidemicRuntimeTime PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicRuntimeTime
    PUBLIC EpidemicRuntimeFoundation
    PRIVATE EpidemicFoundation
)

set(_epidemic_runtime_time_forbidden_targets
    EpidemicRuntimeEnvironment
    EpidemicRuntimeSimulation
    EpidemicRuntimePersistence
    EpidemicRuntimeWorld
)

get_target_property(_epidemic_runtime_time_links EpidemicRuntimeTime LINK_LIBRARIES)
if(_epidemic_runtime_time_links STREQUAL "_epidemic_runtime_time_links-NOTFOUND")
    set(_epidemic_runtime_time_links "")
endif()

foreach(_forbidden_target IN LISTS _epidemic_runtime_time_forbidden_targets)
    if(_epidemic_runtime_time_links)
        list(FIND _epidemic_runtime_time_links "${_forbidden_target}" _forbidden_target_index)
        if(NOT _forbidden_target_index EQUAL -1)
            message(FATAL_ERROR "EpidemicRuntimeTime must not link runtime major '${_forbidden_target}'.")
        endif()
    endif()
endforeach()

epidemic_configure_target(EpidemicRuntimeTime)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
