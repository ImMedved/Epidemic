cmake_minimum_required(VERSION 3.24)

get_filename_component(_repository "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_sandbox "${_repository}/build/architecture-freeze-self-test")
file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/ArchitectureFreeze.cmake" _validator)
file(TO_CMAKE_PATH "${_repository}/EngineFramework/DevelopmentInfrastructure/cmake/FrameworkArchitecture.cmake" _framework_validator)

file(TO_CMAKE_PATH "${_repository}/build" _build_root)
file(TO_CMAKE_PATH "${_sandbox}" _normalized_sandbox)
string(FIND "${_normalized_sandbox}" "${_build_root}/" _sandbox_prefix)
if(NOT _sandbox_prefix EQUAL 0)
    message(FATAL_ERROR "Architecture self-test sandbox escaped the repository build directory")
endif()
file(REMOVE_RECURSE "${_sandbox}")
file(MAKE_DIRECTORY "${_sandbox}")

function(_run_case name root_body module_path module_body header_body expected_result expected_text)
    set(_source "${_sandbox}/${name}/source")
    set(_build "${_sandbox}/${name}/build")
    file(MAKE_DIRECTORY "${_source}/${module_path}")
    file(WRITE "${_source}/CMakeLists.txt"
        "cmake_minimum_required(VERSION 3.24)\nproject(${name} NONE)\n${root_body}\n")
    file(WRITE "${_source}/${module_path}/CMakeLists.txt" "${module_body}\n")
    if(NOT "${header_body}" STREQUAL "")
        file(MAKE_DIRECTORY "${_source}/${module_path}/include")
        file(WRITE "${_source}/${module_path}/include/fixture.h" "#pragma once\n${header_body}\n")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${_source}" -B "${_build}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr)
    set(_output "${_stdout}\n${_stderr}")
    if(expected_result STREQUAL "PASS")
        if(NOT _result EQUAL 0)
            message(FATAL_ERROR "${name}: valid architecture fixture was rejected:\n${_output}")
        endif()
    else()
        if(_result EQUAL 0)
            message(FATAL_ERROR "${name}: invalid architecture fixture was accepted")
        endif()
        string(REGEX REPLACE "[ \t\r\n]+" " " _normalized_output "${_output}")
        string(FIND "${_normalized_output}" "${expected_text}" _match)
        if(_match EQUAL -1)
            message(FATAL_ERROR "${name}: failed for the wrong reason; expected '${expected_text}':\n${_output}")
        endif()
    endif()
endfunction()

set(_validate "include(\"${_validator}\")\nepidemic_validate_architecture_freeze()")

_run_case(base_runtime_link
    "add_library(EpidemicRuntimeForbidden INTERFACE)\nadd_subdirectory(EngineBase/Bad)\n${_validate}"
    "EngineBase/Bad"
    "add_library(BadBase INTERFACE)\ntarget_link_libraries(BadBase INTERFACE EpidemicRuntimeForbidden)"
    ""
    FAIL "EngineBase must not depend on Runtime or Framework")

_run_case(runtime_framework_link
    "add_library(EpidemicGameFrameworkForbidden INTERFACE)\nadd_subdirectory(EngineRuntime/Bad)\n${_validate}"
    "EngineRuntime/Bad"
    "add_library(BadRuntime INTERFACE)\ntarget_link_libraries(BadRuntime INTERFACE EpidemicGameFrameworkForbidden)"
    ""
    FAIL "EngineRuntime must not depend on Framework")

_run_case(runtime_cross_major_link
    "add_library(EpidemicRuntimeWorld INTERFACE)\nadd_subdirectory(EngineRuntime/Scene)\n${_validate}"
    "EngineRuntime/Scene"
    "add_library(EpidemicRuntimeScene INTERFACE)\ntarget_link_libraries(EpidemicRuntimeScene INTERFACE EpidemicRuntimeWorld)"
    ""
    FAIL "directly links Runtime major")

_run_case(framework_core_runtime_link
    "add_library(EpidemicRuntimeWorld INTERFACE)\nadd_subdirectory(EngineFramework/GameplayWorldStateOwners/Bad)\n${_validate}"
    "EngineFramework/GameplayWorldStateOwners/Bad"
    "add_library(BadFrameworkOwner INTERFACE)\ntarget_link_libraries(BadFrameworkOwner INTERFACE EpidemicRuntimeWorld)"
    ""
    FAIL "Framework core must use an approved Runtime boundary")

_run_case(integration_runtime_link
    "add_library(EpidemicRuntimeWorld INTERFACE)\nadd_subdirectory(EngineFramework/IntegrationLayer/Bad)\n${_validate}"
    "EngineFramework/IntegrationLayer/Bad"
    "add_library(BadIntegration INTERFACE)\ntarget_link_libraries(BadIntegration INTERFACE EpidemicRuntimeWorld)"
    ""
    FAIL "has unapproved Runtime dependency")

_run_case(runtime_boundary_link
    "add_library(EpidemicRuntimeRenderer INTERFACE)\nadd_subdirectory(EngineFramework/RuntimeBoundary/Bad)\n${_validate}"
    "EngineFramework/RuntimeBoundary/Bad"
    "add_library(BadRuntimeBoundary INTERFACE)\ntarget_link_libraries(BadRuntimeBoundary INTERFACE EpidemicRuntimeRenderer)"
    ""
    FAIL "has unapproved boundary dependency")

_run_case(base_runtime_include
    "add_subdirectory(EngineBase/Bad)\n${_validate}"
    "EngineBase/Bad"
    "add_library(BadBase INTERFACE)"
    "#include <Epidemic/Runtime/World/world.h>"
    FAIL "EngineBase source references an upper layer")

_run_case(integration_runtime_include
    "set(EPIDEMIC_BUILD_FRAMEWORK ON)\nadd_subdirectory(EngineFramework/IntegrationLayer/Bad)\n${_validate}"
    "EngineFramework/IntegrationLayer/Bad"
    "add_library(BadIntegration INTERFACE)"
    "#include <Epidemic/Runtime/World/world.h>"
    FAIL "Integration source has unapproved Runtime include")

_run_case(runtime_boundary_include
    "set(EPIDEMIC_BUILD_FRAMEWORK ON)\nadd_subdirectory(EngineFramework/RuntimeBoundary/Bad)\n${_validate}"
    "EngineFramework/RuntimeBoundary/Bad"
    "add_library(BadRuntimeBoundary INTERFACE)"
    "#include <Epidemic/Runtime/Renderer/renderer.h>"
    FAIL "RuntimeBoundary source has unapproved Runtime include")

_run_case(valid_base
    "add_subdirectory(EngineBase/Good)\n${_validate}"
    "EngineBase/Good"
    "add_library(GoodBase INTERFACE)"
    ""
    PASS "")

_run_case(framework_horizontal_link
    "add_library(EpidemicGameFrameworkFoundation INTERFACE)\nadd_library(EpidemicGameFrameworkWorld INTERFACE)\nadd_library(EpidemicGameFrameworkEntities INTERFACE)\ntarget_link_libraries(EpidemicGameFrameworkEntities INTERFACE EpidemicGameFrameworkWorld)\ninclude(\"${_framework_validator}\")\nepidemic_validate_framework_architecture()"
    "Fixture"
    "# fixture"
    ""
    FAIL "without central allowlist permission")

file(REMOVE_RECURSE "${_sandbox}")
message(STATUS "Architecture freeze self-test accepted the valid fixture and rejected all 10 forbidden dependency/include fixtures")
