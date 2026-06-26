cmake_minimum_required(VERSION 3.24)

project(EpidemicEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTING "Build Epidemic Engine tests" ON)

add_library(epidemic_core STATIC
    src/core/application/application.cpp
    src/core/config/memory_configuration.cpp
    src/core/diagnostics/console_logger.cpp
    src/core/events/event_bus.cpp
    src/core/modules/module_registry.cpp
    src/core/tasks/task_scheduler.cpp
)

target_include_directories(epidemic_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_library(epidemic_layers_runtime STATIC
    src/layers/runtime/placeholders/null_services.cpp
)

target_include_directories(epidemic_layers_runtime PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(epidemic_layers_runtime
    PUBLIC epidemic_core
)

add_library(epidemic_apps_epidemic_app STATIC
    src/apps/epidemic_app/demo_modules.cpp
)

target_include_directories(epidemic_apps_epidemic_app PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(epidemic_apps_epidemic_app
    PUBLIC epidemic_core epidemic_layers_runtime
)

add_executable(epidemic_app
    src/apps/epidemic_app/main.cpp
)

target_link_libraries(epidemic_app
    PRIVATE epidemic_apps_epidemic_app
)

if(MSVC)
    foreach(target epidemic_core epidemic_layers_runtime epidemic_apps_epidemic_app epidemic_app)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    endforeach()
endif()

if(BUILD_TESTING)
    enable_testing()

    add_executable(epidemic_tests
        tests/test_main.cpp
    )

    target_link_libraries(epidemic_tests
        PRIVATE epidemic_core epidemic_layers_runtime
    )

    if(MSVC)
        target_compile_options(epidemic_tests PRIVATE /W4 /permissive-)
    endif()

    add_test(NAME epidemic_tests COMMAND epidemic_tests)
endif()
