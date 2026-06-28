add_library(EpidemicMemory INTERFACE)

target_include_directories(EpidemicMemory INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicMemory INTERFACE
    EpidemicFoundation
)