cmake_minimum_required(VERSION 3.24)

project(EpidemicEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTING "Build Epidemic Engine tests" ON)
option(EPIDEMIC_BUILD_RUNTIME "Build Epidemic EngineRuntime" ON)
option(EPIDEMIC_BUILD_FRAMEWORK "Build Epidemic GameFramework" ON)
option(EPIDEMIC_WARNINGS_AS_ERRORS "Treat warnings in Epidemic-owned targets as errors" OFF)
option(EPIDEMIC_ARCHITECTURE_FREEZE_CHECKS "Validate frozen layer boundaries and public headers" ON)

if(BUILD_TESTING)
    enable_testing()
endif()

add_subdirectory(EngineBase)

if(EPIDEMIC_BUILD_RUNTIME)
    add_subdirectory(EngineRuntime)
endif()

if(EPIDEMIC_BUILD_FRAMEWORK)
    if(NOT EPIDEMIC_BUILD_RUNTIME)
        message(FATAL_ERROR "EngineFramework requires EPIDEMIC_BUILD_RUNTIME=ON.")
    endif()

    add_subdirectory(EngineFramework)
endif()
if(BUILD_TESTING AND EPIDEMIC_BUILD_RUNTIME AND EPIDEMIC_BUILD_FRAMEWORK)
    add_subdirectory(Tests/Truth)
endif()

if(EPIDEMIC_ARCHITECTURE_FREEZE_CHECKS)
    include(cmake/ArchitectureFreeze.cmake)
    epidemic_validate_architecture_freeze()
    epidemic_add_public_header_self_containment_target()
endif()
