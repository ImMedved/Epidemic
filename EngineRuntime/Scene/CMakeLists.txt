add_library(EpidemicRuntimeScene STATIC
    src/scene.cpp
    src/scene_runtime_impl.cpp
)

target_include_directories(EpidemicRuntimeScene PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicRuntimeScene
    PUBLIC EpidemicRuntimeFoundation
    PRIVATE EpidemicFoundation
)

set(_epidemic_runtime_scene_forbidden_targets
    EpidemicRuntimeWorld
    EpidemicRuntimeRenderer
    EpidemicRuntimePhysics
    EpidemicRuntimeSupport
)

get_target_property(_epidemic_runtime_scene_links EpidemicRuntimeScene LINK_LIBRARIES)
if(_epidemic_runtime_scene_links STREQUAL "_epidemic_runtime_scene_links-NOTFOUND")
    set(_epidemic_runtime_scene_links "")
endif()

foreach(_forbidden_target IN LISTS _epidemic_runtime_scene_forbidden_targets)
    if(_epidemic_runtime_scene_links)
        list(FIND _epidemic_runtime_scene_links "${_forbidden_target}" _forbidden_target_index)
        if(NOT _forbidden_target_index EQUAL -1)
            message(FATAL_ERROR "EpidemicRuntimeScene must not link runtime major '${_forbidden_target}'.")
        endif()
    endif()
endforeach()

epidemic_configure_target(EpidemicRuntimeScene)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
