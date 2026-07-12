add_library(EpidemicRuntimeRenderer STATIC
    src/renderer.cpp
    src/renderer_runtime_impl.cpp
)

target_include_directories(EpidemicRuntimeRenderer PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicRuntimeRenderer
    PUBLIC EpidemicRuntimeFoundation
    PUBLIC EpidemicRuntimeScene
    PUBLIC EpidemicRuntimeResources
    PRIVATE EpidemicFoundation
)

set(_epidemic_runtime_renderer_forbidden_targets
    EpidemicRuntimeWorld
    EpidemicRuntimePhysics
    EpidemicRuntimeSimulation
    EpidemicRuntimeSupport
)

get_target_property(_epidemic_runtime_renderer_links EpidemicRuntimeRenderer LINK_LIBRARIES)
if(_epidemic_runtime_renderer_links STREQUAL "_epidemic_runtime_renderer_links-NOTFOUND")
    set(_epidemic_runtime_renderer_links "")
endif()

foreach(_forbidden_target IN LISTS _epidemic_runtime_renderer_forbidden_targets)
    if(_epidemic_runtime_renderer_links)
        list(FIND _epidemic_runtime_renderer_links "${_forbidden_target}" _forbidden_target_index)
        if(NOT _forbidden_target_index EQUAL -1)
            message(FATAL_ERROR "EpidemicRuntimeRenderer must not link runtime major '${_forbidden_target}'.")
        endif()
    endif()
endforeach()

epidemic_configure_target(EpidemicRuntimeRenderer)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
