add_library(EpidemicGameFrameworkMaterials STATIC
    src/materials.cpp
)

target_include_directories(EpidemicGameFrameworkMaterials PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkMaterials
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkMaterials)

set(_epidemic_materials_forbidden_targets
    EpidemicGameFrameworkEntities
    EpidemicGameFrameworkConditions
    EpidemicGameFrameworkEffects
    EpidemicGameplayQueries
    EpidemicGameplayFacts
    EpidemicGameplayTime
)
get_target_property(_epidemic_materials_links EpidemicGameFrameworkMaterials LINK_LIBRARIES)
if(_epidemic_materials_links STREQUAL "_epidemic_materials_links-NOTFOUND")
    set(_epidemic_materials_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_materials_forbidden_targets)
    list(FIND _epidemic_materials_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "Materials must not link peer/integration major '${_forbidden_target}'.")
    endif()
endforeach()
