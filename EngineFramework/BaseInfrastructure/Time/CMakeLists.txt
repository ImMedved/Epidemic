add_library(EpidemicGameplayTime STATIC
    src/gameplay_time.cpp
)

target_include_directories(EpidemicGameplayTime PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameplayTime
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameplayTime)

set(_epidemic_gameframework_time_forbidden_targets
    EpidemicGameplayQueries
    EpidemicGameplayFacts
    EpidemicGameFrameworkCoreIntegration
)
get_target_property(_epidemic_gameframework_time_links EpidemicGameplayTime LINK_LIBRARIES)
if(_epidemic_gameframework_time_links STREQUAL "_epidemic_gameframework_time_links-NOTFOUND")
    set(_epidemic_gameframework_time_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_gameframework_time_forbidden_targets)
    list(FIND _epidemic_gameframework_time_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "GameplayTime must not link peer major '${_forbidden_target}'.")
    endif()
endforeach()
