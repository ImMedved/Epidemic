add_library(EpidemicGameFrameworkNarrative STATIC
    src/narrative.cpp
)

target_include_directories(EpidemicGameFrameworkNarrative PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkNarrative
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkNarrative)

set(_epidemic_narrative_forbidden_targets
    EpidemicGameFrameworkRuntimeBridge
    EpidemicGameFrameworkAI
    EpidemicGameFrameworkWorld
    EpidemicGameFrameworkCombat
    EpidemicGameFrameworkAbilities
    EpidemicGameFrameworkSociety
    EpidemicGameFrameworkCrime
    EpidemicGameFrameworkLoot
    EpidemicGameFrameworkPopulation
    EpidemicGameFrameworkEncounters
)
get_target_property(_epidemic_narrative_links EpidemicGameFrameworkNarrative LINK_LIBRARIES)
if(_epidemic_narrative_links STREQUAL "_epidemic_narrative_links-NOTFOUND")
    set(_epidemic_narrative_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_narrative_forbidden_targets)
    list(FIND _epidemic_narrative_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "Narrative core must not link peer/upper major '${_forbidden_target}'.")
    endif()
endforeach()
