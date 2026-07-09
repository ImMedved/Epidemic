add_library(EpidemicRuntimePersistence STATIC
    src/persistence.cpp
    src/in_memory_persistence_support.cpp
)

target_include_directories(EpidemicRuntimePersistence PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicRuntimePersistence
    PUBLIC EpidemicRuntimeFoundation
    PRIVATE EpidemicFoundation
)

set(_epidemic_runtime_persistence_forbidden_targets
    EpidemicRuntimeWorld
    EpidemicRuntimeSerialization
    EpidemicRuntimeAssets
)

get_target_property(_epidemic_runtime_persistence_links EpidemicRuntimePersistence LINK_LIBRARIES)
if(_epidemic_runtime_persistence_links STREQUAL "_epidemic_runtime_persistence_links-NOTFOUND")
    set(_epidemic_runtime_persistence_links "")
endif()

foreach(_forbidden_target IN LISTS _epidemic_runtime_persistence_forbidden_targets)
    if(_epidemic_runtime_persistence_links)
        list(FIND _epidemic_runtime_persistence_links "${_forbidden_target}" _forbidden_target_index)
        if(NOT _forbidden_target_index EQUAL -1)
            message(FATAL_ERROR "EpidemicRuntimePersistence must not link runtime major '${_forbidden_target}'.")
        endif()
    endif()
endforeach()

epidemic_configure_target(EpidemicRuntimePersistence)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
