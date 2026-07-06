add_library(EpidemicEngineBaseSupport INTERFACE)

target_include_directories(EpidemicEngineBaseSupport INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../RHI_D3D11/include"
)

target_link_libraries(EpidemicEngineBaseSupport INTERFACE
    EpidemicCore
    EpidemicMemory
    EpidemicDiagnostics
    EpidemicPlatform
    EpidemicInput
    EpidemicRHI
)

target_compile_features(EpidemicEngineBaseSupport INTERFACE cxx_std_20)