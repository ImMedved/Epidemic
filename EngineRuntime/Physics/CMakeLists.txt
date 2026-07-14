add_library(EpidemicRuntimePhysics STATIC
    src/physics.cpp
    src/physics_runtime_impl.cpp
)

target_include_directories(EpidemicRuntimePhysics PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicRuntimePhysics
    PUBLIC EpidemicRuntimeFoundation
    PUBLIC EpidemicRuntimeScene
    PRIVATE EpidemicFoundation
)

set(_epidemic_runtime_physics_forbidden_targets
    EpidemicRuntimeWorld
    EpidemicRuntimeRenderer
    EpidemicRuntimeSimulation
    EpidemicRuntimeSupport
)

get_target_property(_epidemic_runtime_physics_links EpidemicRuntimePhysics LINK_LIBRARIES)
if(_epidemic_runtime_physics_links STREQUAL "_epidemic_runtime_physics_links-NOTFOUND")
    set(_epidemic_runtime_physics_links "")
endif()

foreach(_forbidden_target IN LISTS _epidemic_runtime_physics_forbidden_targets)
    if(_epidemic_runtime_physics_links)
        list(FIND _epidemic_runtime_physics_links "${_forbidden_target}" _forbidden_target_index)
        if(NOT _forbidden_target_index EQUAL -1)
            message(FATAL_ERROR "EpidemicRuntimePhysics must not link runtime major '${_forbidden_target}'.")
        endif()
    endif()
endforeach()

epidemic_configure_target(EpidemicRuntimePhysics)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
