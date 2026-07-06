add_library(EpidemicEngineBaseSupport INTERFACE)

target_include_directories(EpidemicEngineBaseSupport INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicEngineBaseSupport INTERFACE
    EpidemicCore
    EpidemicMemory
    EpidemicDiagnostics
    EpidemicPlatform
    EpidemicInput
    EpidemicRHI
    EpidemicRHI_D3D11
)

target_compile_features(EpidemicEngineBaseSupport INTERFACE cxx_std_20)