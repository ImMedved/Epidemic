add_library(EpidemicRuntimeEnvironment STATIC
    src/environment.cpp
    src/environment_runtime_impl.cpp
)

target_include_directories(EpidemicRuntimeEnvironment PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicRuntimeEnvironment
    PUBLIC EpidemicRuntimeFoundation
    PRIVATE EpidemicFoundation
)

set(_epidemic_runtime_environment_forbidden_targets
    EpidemicRuntimeTime
    EpidemicRuntimeWorld
    EpidemicRuntimeScene
    EpidemicRuntimeRenderer
    EpidemicRuntimePhysics
    EpidemicRuntimeSupport
)

get_target_property(_epidemic_runtime_environment_links EpidemicRuntimeEnvironment LINK_LIBRARIES)
if(_epidemic_runtime_environment_links STREQUAL "_epidemic_runtime_environment_links-NOTFOUND")
    set(_epidemic_runtime_environment_links "")
endif()

foreach(_forbidden_target IN LISTS _epidemic_runtime_environment_forbidden_targets)
    if(_epidemic_runtime_environment_links)
        list(FIND _epidemic_runtime_environment_links "${_forbidden_target}" _forbidden_target_index)
        if(NOT _forbidden_target_index EQUAL -1)
            message(FATAL_ERROR "EpidemicRuntimeEnvironment must not link runtime major '${_forbidden_target}'.")
        endif()
    endif()
endforeach()

epidemic_configure_target(EpidemicRuntimeEnvironment)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
