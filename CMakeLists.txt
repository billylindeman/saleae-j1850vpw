cmake_minimum_required(VERSION 3.13)

project(J1850VpwAnalyzer)

add_definitions(-DLOGIC2)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.14" CACHE STRING "Minimum supported MacOS version" FORCE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(ExternalAnalyzerSDK)

set(ANALYZER_SOURCES
    src/J1850VpwAnalyzer.cpp
    src/J1850VpwAnalyzer.h
    src/J1850VpwAnalyzerResults.cpp
    src/J1850VpwAnalyzerResults.h
    src/J1850VpwAnalyzerSettings.cpp
    src/J1850VpwAnalyzerSettings.h
    src/J1850VpwSimulationDataGenerator.cpp
    src/J1850VpwSimulationDataGenerator.h
)

add_analyzer_plugin(${PROJECT_NAME} SOURCES ${ANALYZER_SOURCES})

option(J1850VPW_BUILD_TESTS "Build unit tests for the J1850 VPW analyzer" ON)
if(J1850VPW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
