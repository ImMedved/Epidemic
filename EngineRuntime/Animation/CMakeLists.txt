add_library(EpidemicRuntimeAnimation STATIC
    src/animation.cpp
    src/animation_runtime_impl.cpp
)

target_include_directories(EpidemicRuntimeAnimation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicRuntimeAnimation
    PUBLIC EpidemicRuntimeFoundation
    PRIVATE EpidemicFoundation
)

set(_epidemic_runtime_animation_forbidden_targets
    EpidemicRuntimeRenderer
    EpidemicRuntimePhysics
    EpidemicRuntimeNavigation
    EpidemicRuntimeSimulation
    EpidemicRuntimeSupport
)

get_target_property(_epidemic_runtime_animation_links EpidemicRuntimeAnimation LINK_LIBRARIES)
if(_epidemic_runtime_animation_links STREQUAL "_epidemic_runtime_animation_links-NOTFOUND")
    set(_epidemic_runtime_animation_links "")
endif()

foreach(_forbidden_target IN LISTS _epidemic_runtime_animation_forbidden_targets)
    if(_epidemic_runtime_animation_links)
        list(FIND _epidemic_runtime_animation_links "${_forbidden_target}" _forbidden_target_index)
        if(NOT _forbidden_target_index EQUAL -1)
            message(FATAL_ERROR "EpidemicRuntimeAnimation must not link runtime major '${_forbidden_target}'.")
        endif()
    endif()
endforeach()

epidemic_configure_target(EpidemicRuntimeAnimation)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
