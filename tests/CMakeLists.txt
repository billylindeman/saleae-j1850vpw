# Tests for the J1850 VPW analyzer.
#
# The SDK ships a real test harness at AnalyzerSDK/testlib/ but its own
# CMakeLists hardcodes a path relative to PROJECT_SOURCE_DIR/AnalyzerSDK that
# doesn't fit our layout. We compile its sources directly into a static lib.

set(TESTLIB_DIR ${PROJECT_SOURCE_DIR}/../AnalyzerSDK/testlib)
set(SDK_INCLUDE ${PROJECT_SOURCE_DIR}/../AnalyzerSDK/include)

if(NOT EXISTS "${TESTLIB_DIR}/TestInstance.h")
    message(FATAL_ERROR "AnalyzerSDK testlib not found at ${TESTLIB_DIR}; tests require a sibling AnalyzerSDK checkout")
endif()

set(TESTHARNESS_SOURCES
    ${TESTLIB_DIR}/AnalyzerStubs.cpp
    ${TESTLIB_DIR}/HelperStubs.cpp
    ${TESTLIB_DIR}/MockChannelData.cpp
    ${TESTLIB_DIR}/MockResults.cpp
    ${TESTLIB_DIR}/MockSimulatedChannelDescriptor.cpp
    ${TESTLIB_DIR}/SettingsStubs.cpp
    ${TESTLIB_DIR}/StreamHelpers.cpp
    ${TESTLIB_DIR}/TestInstance.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FrameV2Stubs.cpp
)

add_library(J1850VpwTestHarness STATIC ${TESTHARNESS_SOURCES})
target_include_directories(J1850VpwTestHarness PUBLIC ${TESTLIB_DIR} ${SDK_INCLUDE})
# Upstream SettingsStubs.cpp uses std::runtime_error without #include <stdexcept>.
# Force-include the header per-platform.
if(MSVC)
    target_compile_options(J1850VpwTestHarness PRIVATE /FI stdexcept)
else()
    target_compile_options(J1850VpwTestHarness PRIVATE -include stdexcept)
endif()

# Sources for the analyzer compiled into the test binaries directly.
# (The SDK testlib supplies its own stubs in place of libAnalyzer, so we do
# NOT link against Saleae::AnalyzerSDK here.)
set(ANALYZER_SOURCES_LOCAL
    ${PROJECT_SOURCE_DIR}/src/J1850VpwAnalyzer.cpp
    ${PROJECT_SOURCE_DIR}/src/J1850VpwAnalyzerResults.cpp
    ${PROJECT_SOURCE_DIR}/src/J1850VpwAnalyzerSettings.cpp
    ${PROJECT_SOURCE_DIR}/src/J1850VpwSimulationDataGenerator.cpp
)

# test_synthetic — pure interval-based unit tests
add_executable(test_synthetic test_synthetic.cpp ${ANALYZER_SOURCES_LOCAL})
target_include_directories(test_synthetic PRIVATE ${PROJECT_SOURCE_DIR}/src ${SDK_INCLUDE} ${TESTLIB_DIR})
target_link_libraries(test_synthetic PRIVATE J1850VpwTestHarness)
target_compile_definitions(test_synthetic PRIVATE LOGIC2)
add_test(NAME synthetic COMMAND test_synthetic)

# test_real_capture — replays the converted P01 bench capture
add_executable(test_real_capture test_real_capture.cpp ${ANALYZER_SOURCES_LOCAL})
target_include_directories(test_real_capture PRIVATE ${PROJECT_SOURCE_DIR}/src ${SDK_INCLUDE} ${TESTLIB_DIR})
target_link_libraries(test_real_capture PRIVATE J1850VpwTestHarness)
target_compile_definitions(test_real_capture PRIVATE
    LOGIC2
    J1850VPW_CAPTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/captures/P01_bench_by_itself.bin")
add_test(NAME real_capture COMMAND test_real_capture)
