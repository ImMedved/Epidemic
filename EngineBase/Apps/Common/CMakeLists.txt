add_library(EpidemicAppSupport INTERFACE)

target_include_directories(EpidemicAppSupport INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicAppSupport INTERFACE
    EpidemicCore
    EpidemicDiagnostics
    EpidemicPlatform
    EpidemicInput
    EpidemicRHI
)

target_compile_features(EpidemicAppSupport INTERFACE cxx_std_20)