add_library(EpidemicGameFrameworkEntities STATIC
    src/entities.cpp
)

target_include_directories(EpidemicGameFrameworkEntities PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkEntities
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkEntities)

set(_epidemic_entities_forbidden_targets
    EpidemicGameFrameworkMaterials
    EpidemicGameFrameworkConditions
    EpidemicGameFrameworkEffects
    EpidemicGameplayQueries
    EpidemicGameplayFacts
    EpidemicGameplayTime
)
get_target_property(_epidemic_entities_links EpidemicGameFrameworkEntities LINK_LIBRARIES)
if(_epidemic_entities_links STREQUAL "_epidemic_entities_links-NOTFOUND")
    set(_epidemic_entities_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_entities_forbidden_targets)
    list(FIND _epidemic_entities_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "Entities must not link peer/integration major '${_forbidden_target}'.")
    endif()
endforeach()
