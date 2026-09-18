add_executable(EpidemicApplicationCompositionTests
    application_composition_tests.cpp
)

target_link_libraries(EpidemicApplicationCompositionTests
    PRIVATE EpidemicFoundation EpidemicMemory EpidemicDiagnostics EpidemicCore EpidemicEngineBaseSupport
)

epidemic_configure_target(EpidemicApplicationCompositionTests)
epidemic_add_test(EpidemicApplicationCompositionTests)

add_executable(EpidemicPlatformInputIntegrationTests
    platform_input_integration_tests.cpp
)

add_library(EpidemicPlatformTestLibrary SHARED
    platform_test_library.cpp
)

set_target_properties(EpidemicPlatformTestLibrary PROPERTIES
    PREFIX ""
)

epidemic_configure_target(EpidemicPlatformTestLibrary)
add_dependencies(EpidemicPlatformInputIntegrationTests EpidemicPlatformTestLibrary)

target_link_libraries(EpidemicPlatformInputIntegrationTests
    PRIVATE EpidemicFoundation EpidemicMemory EpidemicDiagnostics EpidemicCore EpidemicPlatform EpidemicInput EpidemicEngineBaseSupport
)

target_include_directories(EpidemicPlatformInputIntegrationTests PRIVATE
    "${PROJECT_SOURCE_DIR}/EngineBase/Platform/src"
    "${PROJECT_SOURCE_DIR}/EngineBase/Input/src"
)

epidemic_configure_target(EpidemicPlatformInputIntegrationTests)
epidemic_add_test(EpidemicPlatformInputIntegrationTests)

add_executable(EpidemicRhiIntegrationTests
    rhi_integration_tests.cpp
)

target_link_libraries(EpidemicRhiIntegrationTests
    PRIVATE EpidemicFoundation EpidemicMemory EpidemicDiagnostics EpidemicCore EpidemicPlatform EpidemicRHI EpidemicRHI_D3D11 EpidemicEngineBaseSupport
)

if(BUILD_TESTING)
    target_compile_definitions(EpidemicRhiIntegrationTests PRIVATE EPIDEMIC_RHI_D3D11_ENABLE_TEST_HOOKS=1)
    target_include_directories(EpidemicRhiIntegrationTests PRIVATE
        "${PROJECT_SOURCE_DIR}/EngineBase/RHI_D3D11/src"
    )
endif()

epidemic_configure_target(EpidemicRhiIntegrationTests)
epidemic_add_test(EpidemicRhiIntegrationTests)

add_executable(EpidemicSupportIntegrationTests
    support_integration_tests.cpp
)

target_link_libraries(EpidemicSupportIntegrationTests
    PRIVATE EpidemicFoundation EpidemicMemory EpidemicDiagnostics EpidemicCore EpidemicPlatform EpidemicInput EpidemicRHI EpidemicRHI_D3D11 EpidemicEngineBaseSupport
)

epidemic_configure_target(EpidemicSupportIntegrationTests)
epidemic_add_test(EpidemicSupportIntegrationTests)
