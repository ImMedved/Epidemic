if(NOT WIN32)
    message(FATAL_ERROR "Epidemic Engine v1.0 currently supports only Windows for the platform slice.")
endif()

add_library(EpidemicPlatform STATIC
    src/Windows/windows_platform_runtime.cpp
)

target_include_directories(EpidemicPlatform PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_include_directories(EpidemicPlatform PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(EpidemicPlatform
    PUBLIC EpidemicFoundation
    PRIVATE user32 shell32
)

epidemic_configure_target(EpidemicPlatform)
