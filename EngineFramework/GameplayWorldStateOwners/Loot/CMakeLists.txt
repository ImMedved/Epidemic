add_library(EpidemicGameFrameworkLoot STATIC
    src/loot.cpp
)

target_include_directories(EpidemicGameFrameworkLoot PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkLoot
    PUBLIC EpidemicGameFrameworkFoundation EpidemicGameFrameworkSupportRandom
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkLoot)

set(_epidemic_loot_forbidden_targets
    EpidemicGameFrameworkCombat
    EpidemicGameFrameworkAbilities
    EpidemicGameFrameworkProgression
    EpidemicGameFrameworkEffects
    EpidemicGameFrameworkEntities
)
get_target_property(_epidemic_loot_links EpidemicGameFrameworkLoot LINK_LIBRARIES)
if(_epidemic_loot_links STREQUAL "_epidemic_loot_links-NOTFOUND")
    set(_epidemic_loot_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_loot_forbidden_targets)
    list(FIND _epidemic_loot_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "Loot must not link peer/integration major '${_forbidden_target}'.")
    endif()
endforeach()
