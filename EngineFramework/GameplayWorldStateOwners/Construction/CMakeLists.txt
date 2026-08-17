add_library(EpidemicGameFrameworkConstruction STATIC
    src/construction.cpp
)

target_include_directories(EpidemicGameFrameworkConstruction PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkConstruction
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkConstruction)

set(_epidemic_construction_forbidden_targets
    EpidemicGameFrameworkTraversal
    EpidemicGameFrameworkNavigationSemantics
    EpidemicGameFrameworkRuntimeBridge
    EpidemicGameFrameworkWorld
    EpidemicGameFrameworkInteraction
)
get_target_property(_epidemic_construction_links EpidemicGameFrameworkConstruction LINK_LIBRARIES)
if(_epidemic_construction_links STREQUAL "_epidemic_construction_links-NOTFOUND")
    set(_epidemic_construction_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_construction_forbidden_targets)
    list(FIND _epidemic_construction_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "Construction must not link peer major '${_forbidden_target}'.")
    endif()
endforeach()
