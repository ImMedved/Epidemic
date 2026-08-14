add_library(EpidemicGameFrameworkAI STATIC
    src/ai.cpp
)

target_include_directories(EpidemicGameFrameworkAI PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkAI
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkAI)

set(_epidemic_ai_forbidden_targets
    EpidemicGameFrameworkPerception
    EpidemicGameFrameworkKnowledge
    EpidemicGameFrameworkWorld
    EpidemicGameFrameworkCombat
    EpidemicGameFrameworkAbilities
    EpidemicGameFrameworkInteraction
    EpidemicGameFrameworkTraversal
)
get_target_property(_epidemic_ai_links EpidemicGameFrameworkAI LINK_LIBRARIES)
if(_epidemic_ai_links STREQUAL "_epidemic_ai_links-NOTFOUND")
    set(_epidemic_ai_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_ai_forbidden_targets)
    list(FIND _epidemic_ai_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "AI must not link peer/owner major '${_forbidden_target}'.")
    endif()
endforeach()
