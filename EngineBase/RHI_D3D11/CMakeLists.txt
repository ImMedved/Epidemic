add_library(EpidemicRHI_D3D11 STATIC
    src/d3d11_rhi_device.cpp
)

target_include_directories(EpidemicRHI_D3D11 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_include_directories(EpidemicRHI_D3D11 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(EpidemicRHI_D3D11
    PUBLIC EpidemicFoundation EpidemicPlatform EpidemicRHI
    PRIVATE d3d11 dxgi
)

target_compile_features(EpidemicRHI_D3D11 PUBLIC cxx_std_20)

epidemic_configure_target(EpidemicRHI_D3D11)