add_library(EpidemicGameFrameworkNeedsLife STATIC
    src/needs_life.cpp
)

target_include_directories(EpidemicGameFrameworkNeedsLife PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkNeedsLife
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkNeedsLife)

set(_epidemic_needs_life_forbidden_targets
    EpidemicGameFrameworkPopulation
    EpidemicGameFrameworkEncounters
    EpidemicGameFrameworkRolesJobs
    EpidemicGameFrameworkAI
    EpidemicGameFrameworkConditions
    EpidemicGameFrameworkEnvironment
)
get_target_property(_epidemic_needs_life_links EpidemicGameFrameworkNeedsLife LINK_LIBRARIES)
if(_epidemic_needs_life_links STREQUAL "_epidemic_needs_life_links-NOTFOUND")
    set(_epidemic_needs_life_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_needs_life_forbidden_targets)
    list(FIND _epidemic_needs_life_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "NeedsLife must not link peer/upper major '${_forbidden_target}'.")
    endif()
endforeach()
