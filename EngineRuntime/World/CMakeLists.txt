add_library(EpidemicRuntimeWorld STATIC
    src/world.cpp
    src/world_runtime_impl.cpp
)

target_include_directories(EpidemicRuntimeWorld PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicRuntimeWorld
    PUBLIC EpidemicRuntimeFoundation
    PRIVATE EpidemicFoundation
)

set(_epidemic_runtime_world_forbidden_targets
    EpidemicRuntimePersistence
    EpidemicRuntimeScene
    EpidemicRuntimeEnvironment
    EpidemicRuntimeResources
    EpidemicRuntimeSupport
)

get_target_property(_epidemic_runtime_world_links EpidemicRuntimeWorld LINK_LIBRARIES)
if(_epidemic_runtime_world_links STREQUAL "_epidemic_runtime_world_links-NOTFOUND")
    set(_epidemic_runtime_world_links "")
endif()

foreach(_forbidden_target IN LISTS _epidemic_runtime_world_forbidden_targets)
    if(_epidemic_runtime_world_links)
        list(FIND _epidemic_runtime_world_links "${_forbidden_target}" _forbidden_target_index)
        if(NOT _forbidden_target_index EQUAL -1)
            message(FATAL_ERROR "EpidemicRuntimeWorld must not link runtime major '${_forbidden_target}'.")
        endif()
    endif()
endforeach()

epidemic_configure_target(EpidemicRuntimeWorld)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
