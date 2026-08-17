add_library(EpidemicGameplayFacts STATIC
    src/gameplay_facts.cpp
)

target_include_directories(EpidemicGameplayFacts PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameplayFacts
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameplayFacts)

set(_epidemic_gameframework_facts_forbidden_targets
    EpidemicGameplayQueries
    EpidemicGameplayTime
    EpidemicGameFrameworkCoreIntegration
)
get_target_property(_epidemic_gameframework_facts_links EpidemicGameplayFacts LINK_LIBRARIES)
if(_epidemic_gameframework_facts_links STREQUAL "_epidemic_gameframework_facts_links-NOTFOUND")
    set(_epidemic_gameframework_facts_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_gameframework_facts_forbidden_targets)
    list(FIND _epidemic_gameframework_facts_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "GameplayFacts must not link peer major '${_forbidden_target}'.")
    endif()
endforeach()
