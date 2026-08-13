cmake_minimum_required(VERSION 3.24)

project(EpidemicEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTING "Build Epidemic Engine tests" ON)
option(EPIDEMIC_BUILD_RUNTIME "Build Epidemic EngineRuntime" ON)
option(EPIDEMIC_WARNINGS_AS_ERRORS "Treat warnings in Epidemic-owned targets as errors" OFF)

if(BUILD_TESTING)
    enable_testing()
endif()

add_subdirectory(EngineBase)

if(EPIDEMIC_BUILD_RUNTIME)
    add_subdirectory(EngineRuntime)
endif()
