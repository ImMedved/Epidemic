add_subdirectory(Unit)
add_subdirectory(Integration)
add_subdirectory(Regression)

# The four public smoke applications are part of the freeze contract. Their --smoke
# mode creates hidden native windows where applicable and exits after one frame.
foreach(smoke_target IN ITEMS EpidemicHeadlessCoreApp EpidemicWindowSmokeApp EpidemicInputSmokeApp EpidemicRhiClearScreenApp)
    add_test(NAME ${smoke_target} COMMAND $<TARGET_FILE:${smoke_target}> --smoke)
    set_tests_properties(${smoke_target} PROPERTIES TIMEOUT 30)
endforeach()
