add_library(EpidemicGameFrameworkConditions STATIC
    src/conditions.cpp
)

target_include_directories(EpidemicGameFrameworkConditions PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkConditions
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkConditions)

set(_epidemic_conditions_forbidden_targets
    EpidemicGameFrameworkEntities
    EpidemicGameFrameworkMaterials
    EpidemicGameFrameworkEffects
    EpidemicGameplayQueries
    EpidemicGameplayFacts
    EpidemicGameplayTime
)
get_target_property(_epidemic_conditions_links EpidemicGameFrameworkConditions LINK_LIBRARIES)
if(_epidemic_conditions_links STREQUAL "_epidemic_conditions_links-NOTFOUND")
    set(_epidemic_conditions_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_conditions_forbidden_targets)
    list(FIND _epidemic_conditions_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "Conditions must not link peer/integration major '${_forbidden_target}'.")
    endif()
endforeach()
