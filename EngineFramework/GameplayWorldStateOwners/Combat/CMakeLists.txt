add_library(EpidemicGameFrameworkCombat STATIC
    src/combat.cpp
)

target_include_directories(EpidemicGameFrameworkCombat PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkCombat
    PUBLIC EpidemicGameFrameworkFoundation EpidemicGameFrameworkSupportRandom
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkCombat)

set(_epidemic_combat_forbidden_targets
    EpidemicGameFrameworkAbilities
    EpidemicGameFrameworkProgression
    EpidemicGameFrameworkLoot
    EpidemicGameFrameworkEffects
    EpidemicGameFrameworkConditions
    EpidemicGameFrameworkRuntimeBridge
)
get_target_property(_epidemic_combat_links EpidemicGameFrameworkCombat LINK_LIBRARIES)
if(_epidemic_combat_links STREQUAL "_epidemic_combat_links-NOTFOUND")
    set(_epidemic_combat_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_combat_forbidden_targets)
    list(FIND _epidemic_combat_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "Combat must not link peer/integration major '${_forbidden_target}'.")
    endif()
endforeach()
