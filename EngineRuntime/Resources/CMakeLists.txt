add_library(EpidemicRuntimeResources STATIC
    src/resources.cpp
    src/resource_loader_registry.cpp
    src/resource_manager.cpp
)

target_include_directories(EpidemicRuntimeResources PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicRuntimeResources
    PUBLIC EpidemicRuntimeFoundation
)

set(_epidemic_runtime_resources_forbidden_targets
    EpidemicRuntimeAssets
    EpidemicRuntimeSerialization
    EpidemicRuntimeWorld
    EpidemicRuntimeRenderer
)

get_target_property(_epidemic_runtime_resources_links EpidemicRuntimeResources LINK_LIBRARIES)
if(_epidemic_runtime_resources_links STREQUAL "_epidemic_runtime_resources_links-NOTFOUND")
    set(_epidemic_runtime_resources_links "")
endif()

foreach(_forbidden_target IN LISTS _epidemic_runtime_resources_forbidden_targets)
    if(_epidemic_runtime_resources_links)
        list(FIND _epidemic_runtime_resources_links "${_forbidden_target}" _forbidden_target_index)
        if(NOT _forbidden_target_index EQUAL -1)
            message(FATAL_ERROR "EpidemicRuntimeResources must not link runtime major '${_forbidden_target}'.")
        endif()
    endif()
endforeach()

epidemic_configure_target(EpidemicRuntimeResources)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
