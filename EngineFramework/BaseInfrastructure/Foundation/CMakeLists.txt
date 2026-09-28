add_library(EpidemicGameFrameworkFoundation STATIC
    src/gameplay_foundation.cpp
)

target_include_directories(EpidemicGameFrameworkFoundation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(EpidemicGameFrameworkFoundation
    PUBLIC EpidemicFoundation
)

epidemic_configure_target(EpidemicGameFrameworkFoundation)

get_target_property(_epidemic_gameframework_foundation_links EpidemicGameFrameworkFoundation LINK_LIBRARIES)
if(_epidemic_gameframework_foundation_links STREQUAL "_epidemic_gameframework_foundation_links-NOTFOUND")
    set(_epidemic_gameframework_foundation_links "")
endif()
foreach(_link IN LISTS _epidemic_gameframework_foundation_links)
    if(NOT _link STREQUAL "EpidemicFoundation")
        message(FATAL_ERROR "GameFramework Foundation may only link EpidemicFoundation; found '${_link}'.")
    endif()
endforeach()