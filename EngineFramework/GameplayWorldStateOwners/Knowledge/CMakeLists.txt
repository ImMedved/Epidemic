add_library(EpidemicGameFrameworkKnowledge STATIC
    src/knowledge.cpp
)

target_include_directories(EpidemicGameFrameworkKnowledge PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkKnowledge
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkKnowledge)

set(_epidemic_knowledge_forbidden_targets
    EpidemicGameFrameworkPerception
    EpidemicGameFrameworkAI
    EpidemicGameFrameworkSociety
    EpidemicGameFrameworkNarrative
    EpidemicGameFrameworkCombat
)
get_target_property(_epidemic_knowledge_links EpidemicGameFrameworkKnowledge LINK_LIBRARIES)
if(_epidemic_knowledge_links STREQUAL "_epidemic_knowledge_links-NOTFOUND")
    set(_epidemic_knowledge_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_knowledge_forbidden_targets)
    list(FIND _epidemic_knowledge_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "Knowledge must not link peer/upper major '${_forbidden_target}'.")
    endif()
endforeach()
