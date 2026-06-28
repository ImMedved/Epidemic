add_library(EpidemicCore STATIC
    src/application.cpp
    src/event_bus.cpp
    src/memory_configuration.cpp
    src/module_registry.cpp
    src/task_scheduler.cpp
)

target_include_directories(EpidemicCore PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_include_directories(EpidemicCore PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(EpidemicCore
    PUBLIC EpidemicFoundation EpidemicDiagnostics
)

epidemic_configure_target(EpidemicCore)