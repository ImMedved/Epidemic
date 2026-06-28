add_library(EpidemicFoundation INTERFACE)

target_include_directories(EpidemicFoundation INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_features(EpidemicFoundation INTERFACE cxx_std_20)
