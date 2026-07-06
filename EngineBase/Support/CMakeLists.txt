add_library(EpidemicEngineBaseSupport
    src/engine_base_support.cpp
)

target_include_directories(EpidemicEngineBaseSupport
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicEngineBaseSupport
    PUBLIC
        EpidemicCore
        EpidemicMemory
        EpidemicDiagnostics
        EpidemicPlatform
        EpidemicInput
        EpidemicRHI
        EpidemicRHI_D3D11
)

epidemic_configure_target(EpidemicEngineBaseSupport)