add_library(EpidemicGameFrameworkTraversal STATIC
    src/traversal.cpp
)

target_include_directories(EpidemicGameFrameworkTraversal PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkTraversal
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkTraversal)

set(_epidemic_traversal_forbidden_targets
    EpidemicGameFrameworkNavigationSemantics
    EpidemicGameFrameworkConstruction
    EpidemicGameFrameworkRuntimeBridge
    EpidemicGameFrameworkWorld
    EpidemicGameFrameworkCombat
    EpidemicGameFrameworkAbilities
)
get_target_property(_epidemic_traversal_links EpidemicGameFrameworkTraversal LINK_LIBRARIES)
if(_epidemic_traversal_links STREQUAL "_epidemic_traversal_links-NOTFOUND")
    set(_epidemic_traversal_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_traversal_forbidden_targets)
    list(FIND _epidemic_traversal_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "Traversal must not link peer major '${_forbidden_target}'.")
    endif()
endforeach()
