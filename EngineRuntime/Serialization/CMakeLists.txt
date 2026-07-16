add_library(EpidemicRuntimeSerialization STATIC
    src/serialization.cpp
    src/in_memory_archive.cpp
    src/migration_executor.cpp
    src/serializer_registry.cpp
    src/migration_registry.cpp
)

target_include_directories(EpidemicRuntimeSerialization PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicRuntimeSerialization
    PUBLIC EpidemicRuntimeFoundation
    PRIVATE EpidemicFoundation
)

set(_epidemic_runtime_serialization_forbidden_targets
    EpidemicRuntimeAssets
    EpidemicRuntimePersistence
    EpidemicRuntimeWorld
)

get_target_property(_epidemic_runtime_serialization_links EpidemicRuntimeSerialization LINK_LIBRARIES)
if(_epidemic_runtime_serialization_links STREQUAL "_epidemic_runtime_serialization_links-NOTFOUND")
    set(_epidemic_runtime_serialization_links "")
endif()

foreach(_forbidden_target IN LISTS _epidemic_runtime_serialization_forbidden_targets)
    if(_epidemic_runtime_serialization_links)
        list(FIND _epidemic_runtime_serialization_links "${_forbidden_target}" _forbidden_target_index)
        if(NOT _forbidden_target_index EQUAL -1)
            message(FATAL_ERROR "EpidemicRuntimeSerialization must not link runtime major '${_forbidden_target}'.")
        endif()
    endif()
endforeach()

epidemic_configure_target(EpidemicRuntimeSerialization)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
