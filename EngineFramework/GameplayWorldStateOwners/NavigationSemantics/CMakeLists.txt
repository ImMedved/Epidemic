add_library(EpidemicGameFrameworkNavigationSemantics STATIC
    src/navigation_semantics.cpp
)

target_include_directories(EpidemicGameFrameworkNavigationSemantics PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkNavigationSemantics
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkNavigationSemantics)

set(_epidemic_navigation_forbidden_targets
    EpidemicGameFrameworkTraversal
    EpidemicGameFrameworkConstruction
    EpidemicGameFrameworkRuntimeBridge
    EpidemicGameFrameworkWorld
)
get_target_property(_epidemic_navigation_links EpidemicGameFrameworkNavigationSemantics LINK_LIBRARIES)
if(_epidemic_navigation_links STREQUAL "_epidemic_navigation_links-NOTFOUND")
    set(_epidemic_navigation_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_navigation_forbidden_targets)
    list(FIND _epidemic_navigation_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "NavigationSemantics must not link peer major '${_forbidden_target}'.")
    endif()
endforeach()
