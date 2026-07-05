add_library(EpidemicRHI STATIC
    src/null_rhi_device.cpp
)

target_include_directories(EpidemicRHI PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_include_directories(EpidemicRHI PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(EpidemicRHI
    PUBLIC EpidemicFoundation
)

target_compile_features(EpidemicRHI PUBLIC cxx_std_20)

epidemic_configure_target(EpidemicRHI)
