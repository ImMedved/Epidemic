add_library(EpidemicCore STATIC
    src/application.cpp
    src/basic_configuration.cpp
    src/event_bus.cpp
    src/module_registry.cpp
    src/task_scheduler.cpp
)

target_include_directories(EpidemicCore PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicCore
    PUBLIC EpidemicFoundation EpidemicDiagnostics
)

epidemic_configure_target(EpidemicCore)
