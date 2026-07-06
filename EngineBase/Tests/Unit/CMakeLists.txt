add_executable(EpidemicFoundationUnitTests
    foundation_unit_tests.cpp
)

target_link_libraries(EpidemicFoundationUnitTests
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicFoundationUnitTests)
add_test(NAME EpidemicFoundationUnitTests COMMAND EpidemicFoundationUnitTests)

add_executable(EpidemicMemoryUnitTests
    memory_unit_tests.cpp
)

target_link_libraries(EpidemicMemoryUnitTests
    PRIVATE EpidemicFoundation EpidemicMemory
)

epidemic_configure_target(EpidemicMemoryUnitTests)
add_test(NAME EpidemicMemoryUnitTests COMMAND EpidemicMemoryUnitTests)

add_executable(EpidemicDiagnosticsUnitTests
    diagnostics_unit_tests.cpp
)

target_link_libraries(EpidemicDiagnosticsUnitTests
    PRIVATE EpidemicFoundation EpidemicDiagnostics
)

epidemic_configure_target(EpidemicDiagnosticsUnitTests)
add_test(NAME EpidemicDiagnosticsUnitTests COMMAND EpidemicDiagnosticsUnitTests)

add_executable(EpidemicCoreUnitTests
    core_unit_tests.cpp
)

target_link_libraries(EpidemicCoreUnitTests
    PRIVATE EpidemicFoundation EpidemicDiagnostics EpidemicCore EpidemicMemory
)

epidemic_configure_target(EpidemicCoreUnitTests)
add_test(NAME EpidemicCoreUnitTests COMMAND EpidemicCoreUnitTests)

add_executable(EpidemicRhiUnitTests
    rhi_unit_tests.cpp
)

target_link_libraries(EpidemicRhiUnitTests
    PRIVATE EpidemicFoundation EpidemicRHI
)

epidemic_configure_target(EpidemicRhiUnitTests)
add_test(NAME EpidemicRhiUnitTests COMMAND EpidemicRhiUnitTests)