add_library(EpidemicRHI INTERFACE)

target_include_directories(EpidemicRHI INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicRHI INTERFACE
    EpidemicFoundation
)