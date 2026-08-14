add_library(EpidemicGameFrameworkEncounters STATIC
    src/encounters.cpp
)

target_include_directories(EpidemicGameFrameworkEncounters PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkEncounters
    PUBLIC EpidemicGameFrameworkFoundation EpidemicGameFrameworkSupportRandom
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkEncounters)

set(_epidemic_encounters_forbidden_targets
    EpidemicGameFrameworkPopulation
    EpidemicGameFrameworkRolesJobs
    EpidemicGameFrameworkNeedsLife
    EpidemicGameFrameworkCombat
    EpidemicGameFrameworkLoot
)
get_target_property(_epidemic_encounters_links EpidemicGameFrameworkEncounters LINK_LIBRARIES)
if(_epidemic_encounters_links STREQUAL "_epidemic_encounters_links-NOTFOUND")
    set(_epidemic_encounters_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_encounters_forbidden_targets)
    list(FIND _epidemic_encounters_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "Encounters must not link peer/upper major '${_forbidden_target}'.")
    endif()
endforeach()
