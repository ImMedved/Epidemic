add_library(EpidemicRuntimeAssets STATIC
    src/assets.cpp
)

target_include_directories(EpidemicRuntimeAssets PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicRuntimeAssets
    PUBLIC EpidemicRuntimeFoundation
    PRIVATE EpidemicFoundation
)

set(_epidemic_runtime_assets_forbidden_targets
    EpidemicRuntimeResources
    EpidemicRuntimeSerialization
    EpidemicRuntimePersistence
    EpidemicRuntimeTime
    EpidemicRuntimeEnvironment
    EpidemicRuntimeScene
    EpidemicRuntimeWorld
)

get_target_property(_epidemic_runtime_assets_links EpidemicRuntimeAssets LINK_LIBRARIES)
if(_epidemic_runtime_assets_links STREQUAL "_epidemic_runtime_assets_links-NOTFOUND")
    set(_epidemic_runtime_assets_links "")
endif()

foreach(_forbidden_target IN LISTS _epidemic_runtime_assets_forbidden_targets)
    if(_epidemic_runtime_assets_links)
        list(FIND _epidemic_runtime_assets_links "${_forbidden_target}" _forbidden_target_index)
        if(NOT _forbidden_target_index EQUAL -1)
            message(FATAL_ERROR "EpidemicRuntimeAssets must not link runtime major '${_forbidden_target}'.")
        endif()
    endif()
endforeach()

epidemic_configure_target(EpidemicRuntimeAssets)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
