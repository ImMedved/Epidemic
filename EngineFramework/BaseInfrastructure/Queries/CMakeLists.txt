add_library(EpidemicGameplayQueries STATIC
    src/gameplay_queries.cpp
)

target_include_directories(EpidemicGameplayQueries PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameplayQueries
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameplayQueries)

set(_epidemic_gameframework_queries_forbidden_targets
    EpidemicGameplayFacts
    EpidemicGameplayTime
    EpidemicGameFrameworkCoreIntegration
)
get_target_property(_epidemic_gameframework_queries_links EpidemicGameplayQueries LINK_LIBRARIES)
if(_epidemic_gameframework_queries_links STREQUAL "_epidemic_gameframework_queries_links-NOTFOUND")
    set(_epidemic_gameframework_queries_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_gameframework_queries_forbidden_targets)
    list(FIND _epidemic_gameframework_queries_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "GameplayQueries must not link peer major '${_forbidden_target}'.")
    endif()
endforeach()
