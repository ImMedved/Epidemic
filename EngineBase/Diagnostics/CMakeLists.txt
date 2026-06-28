add_library(EpidemicDiagnostics STATIC
    src/console_logger.cpp
)

target_include_directories(EpidemicDiagnostics PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicDiagnostics
    PUBLIC EpidemicFoundation
)

epidemic_configure_target(EpidemicDiagnostics)