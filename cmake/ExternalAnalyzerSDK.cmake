include(FetchContent)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

if (NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY OR NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin/)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin/)
endif()

# Prefer a sibling copy of the AnalyzerSDK at ../AnalyzerSDK relative to the
# project root. Falls back to FetchContent if not present.
set(_local_sdk "${PROJECT_SOURCE_DIR}/../AnalyzerSDK")

if(NOT TARGET Saleae::AnalyzerSDK)
    if(EXISTS "${_local_sdk}/AnalyzerSDKConfig.cmake")
        message(STATUS "Using local Saleae AnalyzerSDK at ${_local_sdk}")
        set(analyzersdk_SOURCE_DIR "${_local_sdk}" CACHE INTERNAL "")
        include("${_local_sdk}/AnalyzerSDKConfig.cmake")
    else()
        FetchContent_Declare(
            analyzersdk
            GIT_REPOSITORY https://github.com/saleae/AnalyzerSDK.git
            GIT_TAG        master
            GIT_SHALLOW    True
            GIT_PROGRESS   True
        )
        FetchContent_GetProperties(analyzersdk)
        if(NOT analyzersdk_POPULATED)
            FetchContent_Populate(analyzersdk)
            include(${analyzersdk_SOURCE_DIR}/AnalyzerSDKConfig.cmake)
            if(APPLE OR WIN32)
                get_target_property(analyzersdk_lib_location Saleae::AnalyzerSDK IMPORTED_LOCATION)
                if(CMAKE_LIBRARY_OUTPUT_DIRECTORY)
                    file(COPY ${analyzersdk_lib_location} DESTINATION ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
                endif()
            endif()
        endif()
    endif()
endif()

function(add_analyzer_plugin TARGET)
    set(options )
    set(single_value_args )
    set(multi_value_args SOURCES)
    cmake_parse_arguments(_p "${options}" "${single_value_args}" "${multi_value_args}" ${ARGN})

    add_library(${TARGET} MODULE ${_p_SOURCES})
    target_link_libraries(${TARGET} PRIVATE Saleae::AnalyzerSDK)

    set(ANALYZER_DESTINATION "Analyzers")
    install(TARGETS ${TARGET} RUNTIME DESTINATION ${ANALYZER_DESTINATION}
                              LIBRARY DESTINATION ${ANALYZER_DESTINATION})

    set_target_properties(${TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${ANALYZER_DESTINATION}
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${ANALYZER_DESTINATION})
endfunction()
