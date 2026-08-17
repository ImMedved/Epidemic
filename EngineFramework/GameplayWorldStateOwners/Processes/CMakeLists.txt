add_library(EpidemicGameFrameworkProcesses STATIC
    src/processes.cpp
)

target_include_directories(EpidemicGameFrameworkProcesses PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkProcesses
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkProcesses)

set(_epidemic_processes_forbidden_targets
    EpidemicGameFrameworkResourcesProduction
    EpidemicGameFrameworkSimulation
    EpidemicGameFrameworkAI
    EpidemicGameFrameworkRuntimeBridge
    EpidemicGameFrameworkItemsInventory
    EpidemicGameFrameworkEconomy
)
get_target_property(_epidemic_processes_links EpidemicGameFrameworkProcesses LINK_LIBRARIES)
if(_epidemic_processes_links STREQUAL "_epidemic_processes_links-NOTFOUND")
    set(_epidemic_processes_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_processes_forbidden_targets)
    list(FIND _epidemic_processes_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "Processes must not link '${_forbidden_target}'.")
    endif()
endforeach()
