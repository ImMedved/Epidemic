add_library(EpidemicRuntimeFoundation STATIC
    src/runtime_foundation.cpp
)

target_include_directories(EpidemicRuntimeFoundation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicRuntimeFoundation
    PUBLIC EpidemicFoundation
)

set(_epidemic_runtime_foundation_forbidden_targets
    EpidemicRuntimeAssets
    EpidemicRuntimeResources
    EpidemicRuntimeSerialization
    EpidemicRuntimePersistence
    EpidemicRuntimeTime
    EpidemicRuntimeEnvironment
    EpidemicRuntimeScene
    EpidemicRuntimeWorld
    EpidemicRuntimeSupport
)

get_target_property(_epidemic_runtime_foundation_links EpidemicRuntimeFoundation LINK_LIBRARIES)
if(_epidemic_runtime_foundation_links STREQUAL "_epidemic_runtime_foundation_links-NOTFOUND")
    set(_epidemic_runtime_foundation_links "")
endif()

foreach(_forbidden_target IN LISTS _epidemic_runtime_foundation_forbidden_targets)
    if(_epidemic_runtime_foundation_links)
        list(FIND _epidemic_runtime_foundation_links "${_forbidden_target}" _forbidden_target_index)
        if(NOT _forbidden_target_index EQUAL -1)
            message(FATAL_ERROR "EpidemicRuntimeFoundation must not link runtime major '${_forbidden_target}'.")
        endif()
    endif()
endforeach()

epidemic_configure_target(EpidemicRuntimeFoundation)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
