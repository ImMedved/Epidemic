cmake_minimum_required(VERSION 3.24)

project(EpidemicEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTING "Build Epidemic Engine tests" ON)

if(BUILD_TESTING)
    enable_testing()
endif()

add_subdirectory(EngineBase)