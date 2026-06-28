add_library(EpidemicRHI_D3D11 INTERFACE)

target_include_directories(EpidemicRHI_D3D11 INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicRHI_D3D11 INTERFACE
    EpidemicFoundation EpidemicRHI
)