add_library(EpidemicGameFrameworkPerception STATIC
    src/perception.cpp
)

target_include_directories(EpidemicGameFrameworkPerception PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkPerception
    PUBLIC EpidemicGameFrameworkFoundation
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkPerception)

set(_epidemic_perception_forbidden_targets
    EpidemicGameFrameworkKnowledge
    EpidemicGameFrameworkAI
    EpidemicGameFrameworkRuntimeBridge
    EpidemicGameFrameworkWorld
    EpidemicGameFrameworkCombat
    EpidemicGameFrameworkAbilities
)
get_target_property(_epidemic_perception_links EpidemicGameFrameworkPerception LINK_LIBRARIES)
if(_epidemic_perception_links STREQUAL "_epidemic_perception_links-NOTFOUND")
    set(_epidemic_perception_links "")
endif()
foreach(_forbidden_target IN LISTS _epidemic_perception_forbidden_targets)
    list(FIND _epidemic_perception_links "${_forbidden_target}" _forbidden_target_index)
    if(NOT _forbidden_target_index EQUAL -1)
        message(FATAL_ERROR "Perception must not link peer/upper major '${_forbidden_target}'.")
    endif()
endforeach()
