add_library(EpidemicGameFrameworkAbilities STATIC
    src/abilities.cpp
)

target_include_directories(EpidemicGameFrameworkAbilities PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkAbilities
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkAbilities)

set(_epidemic_abilities_forbidden_targets
    EpidemicGameFrameworkCombat
    EpidemicGameFrameworkProgression
    EpidemicGameFrameworkLoot
    EpidemicGameFrameworkEffects
    EpidemicGameplayTime
    EpidemicGameFrameworkRuntimeBridge
)
get_target_property(_epidemic_abilities_links EpidemicGameFrameworkAbilities LINK_LIBRARIES)
if(_epidemic_abilities_links STREQUAL "_epidemic_abilities_links-NOTFOUND")
    set(_epidemic_abilities_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_abilities_forbidden_targets)
    list(FIND _epidemic_abilities_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "Abilities must not link peer/integration major '${_forbidden_target}'.")
    endif()
endforeach()
