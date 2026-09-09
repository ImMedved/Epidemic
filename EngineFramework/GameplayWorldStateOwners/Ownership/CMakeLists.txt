add_library(EpidemicGameFrameworkOwnership STATIC
    src/ownership.cpp
)

target_include_directories(EpidemicGameFrameworkOwnership PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkOwnership
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkOwnership)

set(_epidemic_ownership_forbidden_targets
    EpidemicGameFrameworkSociety
    EpidemicGameFrameworkCrime
    EpidemicGameFrameworkAI
    EpidemicGameFrameworkCombat
)
get_target_property(_epidemic_ownership_links EpidemicGameFrameworkOwnership LINK_LIBRARIES)
if(_epidemic_ownership_links STREQUAL "_epidemic_ownership_links-NOTFOUND")
    set(_epidemic_ownership_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_ownership_forbidden_targets)
    list(FIND _epidemic_ownership_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "Ownership must not link peer/upper major '${_forbidden_target}'.")
    endif()
endforeach()
