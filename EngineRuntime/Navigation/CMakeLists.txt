add_library(EpidemicRuntimeNavigation STATIC
    src/navigation.cpp
    src/navigation_runtime_impl.cpp
)

target_include_directories(EpidemicRuntimeNavigation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicRuntimeNavigation
    PUBLIC EpidemicRuntimeFoundation
    PRIVATE EpidemicFoundation
)

set(_epidemic_runtime_navigation_forbidden_targets
    EpidemicRuntimeScene
    EpidemicRuntimePhysics
    EpidemicRuntimeAnimation
    EpidemicRuntimeSimulation
    EpidemicRuntimeSupport
)

get_target_property(_epidemic_runtime_navigation_links EpidemicRuntimeNavigation LINK_LIBRARIES)
if(_epidemic_runtime_navigation_links STREQUAL "_epidemic_runtime_navigation_links-NOTFOUND")
    set(_epidemic_runtime_navigation_links "")
endif()

foreach(_forbidden_target IN LISTS _epidemic_runtime_navigation_forbidden_targets)
    if(_epidemic_runtime_navigation_links)
        list(FIND _epidemic_runtime_navigation_links "${_forbidden_target}" _forbidden_target_index)
        if(NOT _forbidden_target_index EQUAL -1)
            message(FATAL_ERROR "EpidemicRuntimeNavigation must not link runtime major '${_forbidden_target}'.")
        endif()
    endif()
endforeach()

epidemic_configure_target(EpidemicRuntimeNavigation)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
