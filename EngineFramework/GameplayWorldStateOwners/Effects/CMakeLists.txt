add_library(EpidemicGameFrameworkEffects STATIC
    src/effects.cpp
)

target_include_directories(EpidemicGameFrameworkEffects PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkEffects
    PUBLIC EpidemicGameFrameworkFoundation EpidemicCore
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkEffects)

set(_epidemic_effects_forbidden_targets
    EpidemicGameFrameworkEntities
    EpidemicGameFrameworkMaterials
    EpidemicGameFrameworkConditions
    EpidemicGameplayQueries
    EpidemicGameplayFacts
    EpidemicGameplayTime
)
get_target_property(_epidemic_effects_links EpidemicGameFrameworkEffects LINK_LIBRARIES)
if(_epidemic_effects_links STREQUAL "_epidemic_effects_links-NOTFOUND")
    set(_epidemic_effects_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_effects_forbidden_targets)
    list(FIND _epidemic_effects_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "Effects must not link peer/integration major '${_forbidden_target}'.")
    endif()
endforeach()
