add_library(EpidemicGameFrameworkFoundation STATIC
    src/gameplay_foundation.cpp
)

target_include_directories(EpidemicGameFrameworkFoundation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkFoundation
    PUBLIC EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkFoundation)

set(_epidemic_gameframework_foundation_forbidden_targets
    EpidemicGameplayQueries
    EpidemicGameplayFacts
    EpidemicGameplayTime
    EpidemicGameFrameworkCoreIntegration
)
get_target_property(_epidemic_gameframework_foundation_links EpidemicGameFrameworkFoundation LINK_LIBRARIES)
if(_epidemic_gameframework_foundation_links STREQUAL "_epidemic_gameframework_foundation_links-NOTFOUND")
    set(_epidemic_gameframework_foundation_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_gameframework_foundation_forbidden_targets)
    list(FIND _epidemic_gameframework_foundation_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "GameFramework Foundation must not link '${_forbidden_target}'.")
    endif()
endforeach()
