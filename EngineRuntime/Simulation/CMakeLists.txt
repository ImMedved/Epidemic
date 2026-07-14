add_library(EpidemicRuntimeSimulation STATIC
    src/simulation.cpp
    src/simulation_runtime_impl.cpp
)

target_include_directories(EpidemicRuntimeSimulation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicRuntimeSimulation
    PUBLIC EpidemicRuntimeFoundation
    PUBLIC EpidemicRuntimeTime
    PRIVATE EpidemicFoundation
)

set(_epidemic_runtime_simulation_forbidden_targets
    EpidemicRuntimeRenderer
    EpidemicRuntimePhysics
    EpidemicRuntimeNavigation
    EpidemicRuntimeAnimation
    EpidemicRuntimeAudio
    EpidemicRuntimeSupport
)

get_target_property(_epidemic_runtime_simulation_links EpidemicRuntimeSimulation LINK_LIBRARIES)
if(_epidemic_runtime_simulation_links STREQUAL "_epidemic_runtime_simulation_links-NOTFOUND")
    set(_epidemic_runtime_simulation_links "")
endif()

foreach(_forbidden_target IN LISTS _epidemic_runtime_simulation_forbidden_targets)
    if(_epidemic_runtime_simulation_links)
        list(FIND _epidemic_runtime_simulation_links "${_forbidden_target}" _forbidden_target_index)
        if(NOT _forbidden_target_index EQUAL -1)
            message(FATAL_ERROR "EpidemicRuntimeSimulation must not link runtime major '${_forbidden_target}'.")
        endif()
    endif()
endforeach()

epidemic_configure_target(EpidemicRuntimeSimulation)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
