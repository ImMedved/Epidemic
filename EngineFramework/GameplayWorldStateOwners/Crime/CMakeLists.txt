add_library(EpidemicGameFrameworkCrime STATIC
    src/crime.cpp
)

target_include_directories(EpidemicGameFrameworkCrime PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkCrime
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkCrime)

set(_epidemic_crime_forbidden_targets
    EpidemicGameFrameworkOwnership
    EpidemicGameFrameworkSociety
    EpidemicGameFrameworkAI
    EpidemicGameFrameworkCombat
    EpidemicGameFrameworkInteraction
)
get_target_property(_epidemic_crime_links EpidemicGameFrameworkCrime LINK_LIBRARIES)
if(_epidemic_crime_links STREQUAL "_epidemic_crime_links-NOTFOUND")
    set(_epidemic_crime_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_crime_forbidden_targets)
    list(FIND _epidemic_crime_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "Crime must not link peer/upper major '${_forbidden_target}'.")
    endif()
endforeach()
