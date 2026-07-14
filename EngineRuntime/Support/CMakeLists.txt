add_library(EpidemicRuntimeSupport STATIC
    src/runtime_support.cpp
)

target_include_directories(EpidemicRuntimeSupport PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicRuntimeSupport
    PUBLIC EpidemicCore
    PUBLIC EpidemicRuntimeFoundation
    PUBLIC EpidemicRuntimeAssets
    PUBLIC EpidemicRuntimeSerialization
    PUBLIC EpidemicRuntimeResources
    PUBLIC EpidemicRuntimePersistence
    PUBLIC EpidemicRuntimeEnvironment
    PUBLIC EpidemicRuntimeScene
    PUBLIC EpidemicRuntimeRenderer
    PRIVATE EpidemicFoundation
)

epidemic_configure_target(EpidemicRuntimeSupport)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
