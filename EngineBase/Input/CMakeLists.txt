add_library(EpidemicInput STATIC
    src/input_system.cpp
)

target_include_directories(EpidemicInput PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_include_directories(EpidemicInput PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(EpidemicInput
    PUBLIC EpidemicFoundation EpidemicPlatform
)

target_compile_features(EpidemicInput PUBLIC cxx_std_20)

epidemic_configure_target(EpidemicInput)
