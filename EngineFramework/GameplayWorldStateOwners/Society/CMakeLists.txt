add_library(EpidemicGameFrameworkSociety STATIC
    src/society.cpp
)

target_include_directories(EpidemicGameFrameworkSociety PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkSociety
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkSociety)

set(_epidemic_society_forbidden_targets
    EpidemicGameFrameworkOwnership
    EpidemicGameFrameworkCrime
    EpidemicGameFrameworkAI
    EpidemicGameFrameworkNarrative
)
get_target_property(_epidemic_society_links EpidemicGameFrameworkSociety LINK_LIBRARIES)
if(_epidemic_society_links STREQUAL "_epidemic_society_links-NOTFOUND")
    set(_epidemic_society_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_society_forbidden_targets)
    list(FIND _epidemic_society_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "Society must not link peer/upper major '${_forbidden_target}'.")
    endif()
endforeach()
