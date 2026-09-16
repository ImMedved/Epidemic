add_executable(EpidemicFoundationUnitTests
    foundation_unit_tests.cpp
)

target_link_libraries(EpidemicFoundationUnitTests
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicFoundationUnitTests)
epidemic_add_test(EpidemicFoundationUnitTests)

add_executable(EpidemicMemoryUnitTests
    memory_unit_tests.cpp
)

target_link_libraries(EpidemicMemoryUnitTests
    PRIVATE EpidemicFoundation EpidemicMemory
)

epidemic_configure_target(EpidemicMemoryUnitTests)
epidemic_add_test(EpidemicMemoryUnitTests)

add_executable(EpidemicDiagnosticsUnitTests
    diagnostics_unit_tests.cpp
)

target_link_libraries(EpidemicDiagnosticsUnitTests
    PRIVATE EpidemicFoundation EpidemicDiagnostics
)

epidemic_configure_target(EpidemicDiagnosticsUnitTests)
epidemic_add_test(EpidemicDiagnosticsUnitTests)

add_executable(EpidemicCoreUnitTests
    core_unit_tests.cpp
)

target_link_libraries(EpidemicCoreUnitTests
    PRIVATE EpidemicFoundation EpidemicDiagnostics EpidemicCore EpidemicMemory
)

epidemic_configure_target(EpidemicCoreUnitTests)
epidemic_add_test(EpidemicCoreUnitTests)

add_executable(EpidemicRhiUnitTests
    rhi_unit_tests.cpp
)

target_link_libraries(EpidemicRhiUnitTests
    PRIVATE EpidemicFoundation EpidemicRHI
)

epidemic_configure_target(EpidemicRhiUnitTests)
epidemic_add_test(EpidemicRhiUnitTests)