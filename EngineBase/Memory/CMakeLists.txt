add_library(EpidemicMemory STATIC
    src/memory_tracker.cpp
)

target_include_directories(EpidemicMemory PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicMemory
    PUBLIC EpidemicFoundation
)

epidemic_configure_target(EpidemicMemory)