add_library(EpidemicRuntimeFoundation STATIC
    src/runtime_foundation.cpp
)

target_include_directories(EpidemicRuntimeFoundation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicRuntimeFoundation
    PUBLIC EpidemicFoundation
)

epidemic_configure_target(EpidemicRuntimeFoundation)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
