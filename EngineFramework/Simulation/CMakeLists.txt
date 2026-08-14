add_library(EpidemicGameFrameworkSimulation STATIC
    src/simulation.cpp
)

target_include_directories(EpidemicGameFrameworkSimulation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkSimulation
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkSimulation)

set(_epidemic_simulation_forbidden_targets
    EpidemicGameFrameworkProcesses
    EpidemicGameFrameworkResourcesProduction
    EpidemicGameFrameworkAI
    EpidemicGameFrameworkRuntimeBridge
)
get_target_property(_epidemic_simulation_links EpidemicGameFrameworkSimulation LINK_LIBRARIES)
if(_epidemic_simulation_links STREQUAL "_epidemic_simulation_links-NOTFOUND")
    set(_epidemic_simulation_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_simulation_forbidden_targets)
    list(FIND _epidemic_simulation_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "Simulation must not link '${_forbidden_target}'.")
    endif()
endforeach()
