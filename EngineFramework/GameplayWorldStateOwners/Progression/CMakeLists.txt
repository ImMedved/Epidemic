add_library(EpidemicGameFrameworkProgression STATIC
    src/progression.cpp
)

target_include_directories(EpidemicGameFrameworkProgression PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkProgression
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkProgression)

set(_epidemic_progression_forbidden_targets
    EpidemicGameFrameworkCombat
    EpidemicGameFrameworkAbilities
    EpidemicGameFrameworkLoot
    EpidemicGameFrameworkEffects
    EpidemicGameFrameworkConditions
)
get_target_property(_epidemic_progression_links EpidemicGameFrameworkProgression LINK_LIBRARIES)
if(_epidemic_progression_links STREQUAL "_epidemic_progression_links-NOTFOUND")
    set(_epidemic_progression_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_progression_forbidden_targets)
    list(FIND _epidemic_progression_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "Progression must not link peer major '${_forbidden_target}'.")
    endif()
endforeach()
