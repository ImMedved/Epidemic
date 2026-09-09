add_library(EpidemicGameFrameworkPerceptionKnowledgeAIIntegration STATIC
    src/perception_knowledge_ai_adapters.cpp
)

target_include_directories(EpidemicGameFrameworkPerceptionKnowledgeAIIntegration PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkPerceptionKnowledgeAIIntegration
    PUBLIC
        EpidemicGameFrameworkPerception
        EpidemicGameFrameworkKnowledge
        EpidemicGameFrameworkAI
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkPerceptionKnowledgeAIIntegration)
