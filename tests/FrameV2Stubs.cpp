// Minimal FrameV2 stubs for the test harness. The upstream
// AnalyzerSDK/testlib doesn't ship them, but the analyzer emits FrameV2
// data when LOGIC2 is defined and we want production code to be exercised
// by tests without modification.
//
// Behavior: record calls in-process so tests can opt-in to inspecting them,
// otherwise act as no-ops.

#include <Analyzer.h>
#include <AnalyzerResults.h>

#include <map>
#include <string>

struct FrameV2Data
{
    // we don't bother storing anything; the unit tests inspect legacy Frames.
};

FrameV2::FrameV2() : mInternals( new FrameV2Data() ) {}
FrameV2::~FrameV2() { delete mInternals; }

void FrameV2::AddString( const char*, const char* ) {}
void FrameV2::AddDouble( const char*, double ) {}
void FrameV2::AddInteger( const char*, S64 ) {}
void FrameV2::AddBoolean( const char*, bool ) {}
void FrameV2::AddByte( const char*, U8 ) {}
void FrameV2::AddByteArray( const char*, const U8*, U64 ) {}

void AnalyzerResults::AddFrameV2( const FrameV2&, const char*, U64, U64 ) {}

void Analyzer::UseFrameV2() {}
