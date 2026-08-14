add_library(EpidemicGameFrameworkResourcesProduction STATIC
    src/resources_production.cpp
)

target_include_directories(EpidemicGameFrameworkResourcesProduction PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkResourcesProduction
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkResourcesProduction)

set(_epidemic_resources_forbidden_targets
    EpidemicGameFrameworkProcesses
    EpidemicGameFrameworkSimulation
    EpidemicGameFrameworkAI
    EpidemicGameFrameworkRuntimeBridge
    EpidemicGameFrameworkItemsInventory
    EpidemicGameFrameworkEconomy
)
get_target_property(_epidemic_resources_links EpidemicGameFrameworkResourcesProduction LINK_LIBRARIES)
if(_epidemic_resources_links STREQUAL "_epidemic_resources_links-NOTFOUND")
    set(_epidemic_resources_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_resources_forbidden_targets)
    list(FIND _epidemic_resources_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "ResourcesProduction must not link '${_forbidden_target}'.")
    endif()
endforeach()
