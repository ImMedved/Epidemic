add_library(EpidemicGameFrameworkSupportRandom STATIC
    src/deterministic_random.cpp
)

target_include_directories(EpidemicGameFrameworkSupportRandom PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkSupportRandom PUBLIC EpidemicGameFrameworkFoundation)
epidemic_configure_target(EpidemicGameFrameworkSupportRandom)
