add_library(EpidemicGameFrameworkRolesJobs STATIC
    src/roles_jobs.cpp
)

target_include_directories(EpidemicGameFrameworkRolesJobs PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkRolesJobs
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkRolesJobs)

set(_epidemic_roles_jobs_forbidden_targets
    EpidemicGameFrameworkPopulation
    EpidemicGameFrameworkEncounters
    EpidemicGameFrameworkNeedsLife
    EpidemicGameFrameworkSociety
    EpidemicGameFrameworkOwnership
    EpidemicGameFrameworkAI
)
get_target_property(_epidemic_roles_jobs_links EpidemicGameFrameworkRolesJobs LINK_LIBRARIES)
if(_epidemic_roles_jobs_links STREQUAL "_epidemic_roles_jobs_links-NOTFOUND")
    set(_epidemic_roles_jobs_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_roles_jobs_forbidden_targets)
    list(FIND _epidemic_roles_jobs_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "RolesJobs must not link peer/upper major '${_forbidden_target}'.")
    endif()
endforeach()
