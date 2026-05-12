// Replay a converted .sr capture through the J1850 VPW analyzer.
// The .sr -> .bin conversion is done by tests/convert_sr.py; this test loads
// the resulting binary and feeds its transitions into the analyzer.

#include "MockChannelData.h"
#include "MockResults.h"
#include "TestInstance.h"
#include "TestMacros.h"

#include "J1850VpwAnalyzer.h"
#include "J1850VpwAnalyzerResults.h"
#include "J1850VpwAnalyzerSettings.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

#ifndef J1850VPW_CAPTURE_PATH
#define J1850VPW_CAPTURE_PATH "captures/P01_bench_by_itself.bin"
#endif

using namespace AnalyzerTest;

namespace
{

struct Capture
{
    uint64_t sample_rate;
    uint8_t initial_bit;
    std::vector<uint64_t> transitions;
};

bool loadCapture( const char* path, Capture& out )
{
    std::ifstream f( path, std::ios::binary );
    if( !f )
    {
        std::cerr << "ERROR: cannot open capture file " << path << "\n";
        std::cerr << "       run: python3 tests/convert_sr.py "
                     "/home/billy/Development/sig/P01_bench_by_itself.sr "
                     "tests/captures/P01_bench_by_itself.bin\n";
        return false;
    }
    uint64_t rate = 0, n = 0;
    uint8_t initial = 0;
    f.read( reinterpret_cast<char*>( &rate ), sizeof rate );
    f.read( reinterpret_cast<char*>( &n ), sizeof n );
    f.read( reinterpret_cast<char*>( &initial ), sizeof initial );
    if( !f )
    {
        std::cerr << "ERROR: corrupt header in " << path << "\n";
        return false;
    }
    out.sample_rate = rate;
    out.initial_bit = initial;
    out.transitions.resize( n );
    if( n > 0 )
        f.read( reinterpret_cast<char*>( out.transitions.data() ), n * sizeof( uint64_t ) );
    return static_cast<bool>( f );
}

}  // namespace

int main( int /*argc*/, char** /*argv*/ )
{
    Capture cap;
    if( !loadCapture( J1850VPW_CAPTURE_PATH, cap ) )
    {
        // Treat a missing capture as a skip rather than failure — the
        // converter has to be run once after cloning. Return success but
        // print a clear notice.
        std::cerr << "test_real_capture: SKIPPED (capture not generated)\n";
        return EXIT_SUCCESS;
    }

    std::cout << "loaded " << J1850VPW_CAPTURE_PATH << "\n"
              << "  sample rate:  " << cap.sample_rate << " Hz\n"
              << "  transitions:  " << cap.transitions.size() << "\n"
              << "  initial bit:  " << int( cap.initial_bit ) << "\n";

    Instance inst;
    inst.CreatePlugin( "SAE J1850 VPW" );
    inst.SetSampleRate( cap.sample_rate );

    auto* settings = static_cast<J1850VpwAnalyzerSettings*>( inst.GetSettings() );
    settings->mInputChannel = Channel( 0, 0, DIGITAL_CHANNEL );
    // P01 bench capture idles LOW and pulses HIGH — active-high polarity.
    settings->mActiveLevel = 1;
    settings->mVerifyChecksum = true;

    auto* cd = new MockChannelData( &inst );
    cd->TestSetInitialBitState( cap.initial_bit ? BIT_HIGH : BIT_LOW );
    // MockChannelData::TestAppendTransitions interprets values as deltas
    // from the previous sample, but the .bin stores absolute positions.
    // Convert here.
    std::vector<U64> deltas;
    deltas.reserve( cap.transitions.size() );
    U64 prev = 0;
    for( U64 abs : cap.transitions )
    {
        deltas.push_back( abs - prev );
        prev = abs;
    }
    cd->TestAppendTransitions( deltas );
    cd->ResetCurrentSample();
    inst.SetChannelData( Channel( 0, 0, DIGITAL_CHANNEL ), cd );

    auto result = inst.RunAnalyzerWorker( 60 );
    TEST_VERIFY( result == Instance::WorkerRanOutOfData );

    auto* res = inst.GetResults();
    // AnalyzerResults::GetNumFrames has an off-by-one in MockResults; reach
    // into the MockResultData directly.
    auto* mock = AnalyzerTest::MockResultData::MockFromResults( res );
    U64 nframes = mock->TotalFrameCount();
    std::cout << "  produced " << nframes << " frames\n";

    int sof = 0, eof_ = 0, ifs = 0, errs = 0, csumOk = 0, csumBad = 0;
    int speed1 = 0, speed4 = 0;
    int prioCount = 0, dataCount = 0;
    for( U64 i = 0; i < nframes; ++i )
    {
        Frame f = mock->GetFrame( i );
        switch( f.mType )
        {
            case J1850_SOF:
                sof++;
                if( f.mData1 == 1 ) speed1++;
                else if( f.mData1 == 4 ) speed4++;
                break;
            case J1850_EOF: eof_++; break;
            case J1850_IFS: ifs++; break;
            case J1850_ERROR: errs++; break;
            case J1850_CSUM:
                if( f.HasFlag( DISPLAY_AS_ERROR_FLAG ) ) csumBad++;
                else csumOk++;
                break;
            case J1850_PRIO: prioCount++; break;
            case J1850_DATA: dataCount++; break;
        }
    }

    std::cout << "  SOF=" << sof
              << " (1x=" << speed1 << ", 4x=" << speed4 << ")"
              << " EOF=" << eof_
              << " IFS=" << ifs
              << " ERR=" << errs
              << " CSUM ok=" << csumOk << " bad=" << csumBad
              << " PRIO=" << prioCount << " DATA=" << dataCount
              << "\n";

    TEST_VERIFY( sof > 0 );
    TEST_VERIFY( csumOk > 0 );
    // P01 is a clean bench capture — we expect very few or no CRC failures.
    // Allow a tiny number to account for capture-edge effects (start/end).
    TEST_VERIFY( csumBad <= 2 );
    // Speed should be 1x for this capture (10.4 kbps).
    TEST_VERIFY( speed1 > 0 );

    std::cout << "real_capture: OK\n";
    return EXIT_SUCCESS;
}
