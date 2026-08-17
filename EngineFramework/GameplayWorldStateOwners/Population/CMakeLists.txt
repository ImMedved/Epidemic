add_library(EpidemicGameFrameworkPopulation STATIC
    src/population.cpp
)

target_include_directories(EpidemicGameFrameworkPopulation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkPopulation
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkPopulation)

set(_epidemic_population_forbidden_targets
    EpidemicGameFrameworkEncounters
    EpidemicGameFrameworkRolesJobs
    EpidemicGameFrameworkNeedsLife
    EpidemicGameFrameworkAI
)
get_target_property(_epidemic_population_links EpidemicGameFrameworkPopulation LINK_LIBRARIES)
if(_epidemic_population_links STREQUAL "_epidemic_population_links-NOTFOUND")
    set(_epidemic_population_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_population_forbidden_targets)
    list(FIND _epidemic_population_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "Population must not link peer/upper major '${_forbidden_target}'.")
    endif()
endforeach()
