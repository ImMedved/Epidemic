function(epidemic_configure_target target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_20)

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
        # Keep per-target compiler PDBs below the Windows path-length limit.  Long
        # Framework target names otherwise place the default PDB path beyond what
        # cl.exe can reliably open during parallel Debug builds.
        set_target_properties(${target_name} PROPERTIES
            COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb"
            COMPILE_PDB_NAME "${target_name}"
        )
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_definitions(${target_name} PRIVATE _USE_STD_VECTOR_ALGORITHMS=0)
        endif()
        if(EPIDEMIC_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
        if(EPIDEMIC_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_subdirectory(Foundation)
add_subdirectory(Memory)
add_subdirectory(Diagnostics)
add_subdirectory(Core)
add_subdirectory(Platform)
add_subdirectory(Input)
add_subdirectory(RHI)
add_subdirectory(RHI_D3D11)
add_subdirectory(Support)
add_subdirectory(Apps)

if(BUILD_TESTING)
    add_subdirectory(Tests)
endif()
