add_library(EpidemicGameFrameworkSaveGame STATIC src/save_game.cpp)
target_include_directories(EpidemicGameFrameworkSaveGame PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(EpidemicGameFrameworkSaveGame PUBLIC EpidemicGameFrameworkFoundation PRIVATE EpidemicFoundation)
epidemic_configure_target(EpidemicGameFrameworkSaveGame)

set(_epidemic_save_game_forbidden_targets
    EpidemicGameFrameworkEntities
    EpidemicGameFrameworkWorld
    EpidemicGameFrameworkInteraction
    EpidemicGameFrameworkProgression
    EpidemicGameFrameworkCombat
    EpidemicGameFrameworkAbilities
    EpidemicGameFrameworkLoot
    EpidemicGameFrameworkTraversal
    EpidemicGameFrameworkNavigationSemantics
    EpidemicGameFrameworkConstruction
    EpidemicGameFrameworkPerception
    EpidemicGameFrameworkKnowledge
    EpidemicGameFrameworkAI
    EpidemicGameFrameworkOwnership
    EpidemicGameFrameworkSociety
    EpidemicGameFrameworkCrime
    EpidemicGameFrameworkPopulation
    EpidemicGameFrameworkEncounters
    EpidemicGameFrameworkRolesJobs
    EpidemicGameFrameworkNeedsLife
    EpidemicGameFrameworkProcesses
    EpidemicGameFrameworkResourcesProduction
    EpidemicGameFrameworkSimulation
    EpidemicGameFrameworkNarrative
    EpidemicGameFrameworkItemsInventory
    EpidemicGameFrameworkEquipment
    EpidemicGameFrameworkDialogue
    EpidemicGameFrameworkEconomy
)
get_target_property(_epidemic_save_game_links EpidemicGameFrameworkSaveGame LINK_LIBRARIES)
if(_epidemic_save_game_links STREQUAL "_epidemic_save_game_links-NOTFOUND")
    set(_epidemic_save_game_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_save_game_forbidden_targets)
    list(FIND _epidemic_save_game_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "SaveGame core must not link gameplay major '${_forbidden_target}'.")
    endif()
endforeach()
