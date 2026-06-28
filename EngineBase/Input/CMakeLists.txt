add_library(EpidemicInput INTERFACE)

target_include_directories(EpidemicInput INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicInput INTERFACE
    EpidemicFoundation
)