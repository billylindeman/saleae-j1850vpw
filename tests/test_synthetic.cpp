// Synthetic timing tests for the J1850 VPW analyzer. These run quickly and
// do not depend on any captured data. We replay precise microsecond-spaced
// transitions through MockChannelData and inspect the produced Frames.

#include "MockChannelData.h"
#include "MockResults.h"
#include "TestInstance.h"
#include "TestMacros.h"

#include "J1850VpwAnalyzer.h"
#include "J1850VpwAnalyzerResults.h"
#include "J1850VpwAnalyzerSettings.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace AnalyzerTest;

namespace
{

constexpr U64 kSampleRate = 16000000;  // 16 MHz, matches our real capture

constexpr double _us( double v ) { return v * 1e-6; }

// J1850 CRC-8 (poly 0x1D, init 0xFF, xor-out 0xFF)
U8 crc8( const std::vector<U8>& bytes )
{
    U8 c = 0xFF;
    for( U8 b : bytes )
    {
        c ^= b;
        for( int i = 0; i < 8; ++i )
            c = ( c & 0x80 ) ? U8( ( c << 1 ) ^ 0x1D ) : U8( c << 1 );
    }
    return U8( c ^ 0xFF );
}

// Build inter-edge intervals (in seconds) representing a VPW frame.
//
// MockChannelData::TestAppendIntervals semantics: the channel starts at
// `initial_bit_state`; after each interval the line toggles. So intervals
// describe the dwell time AT THE CURRENT LEVEL before the line transitions
// to the opposite level.
//
// `lastLevelActive` tracks the level the most recent interval held the line
// at. The next interval's level is therefore the opposite.
struct FrameBuilder
{
    std::vector<double> intervals;
    bool lastLevelActive;     // false = the line is currently at passive (initial state)
    int spd;

    explicit FrameBuilder( int speed ) : spd( speed ), lastLevelActive( false ) {}

    void sof()
    {
        // initial passive idle
        intervals.push_back( _us( 1000.0 ) );
        lastLevelActive = false;
        // SOF active dwell
        intervals.push_back( _us( 200.0 / spd ) );
        lastLevelActive = true;
    }

    // SAE J1850 VPW bit encoding (rules apply to the pulse level just ended):
    //   active short  = 1     active long  = 0
    //   passive short = 0     passive long = 1
    void bit( U8 b )
    {
        bool nextActive = !lastLevelActive;
        double w;
        if( nextActive )
            w = b ? 64.0 / spd : 128.0 / spd;
        else
            w = b ? 128.0 / spd : 64.0 / spd;
        intervals.push_back( _us( w ) );
        lastLevelActive = nextActive;
    }

    void byte( U8 v )
    {
        for( int i = 7; i >= 0; --i ) bit( ( v >> i ) & 1u );
    }

    void eof()
    {
        // Long passive idle. If the line is currently active, the next
        // interval transitions it to passive. If currently passive, we
        // need an even-numbered pair to land back at passive, but since
        // intervals always toggle, just push one long idle — the decoder's
        // IFS detection (pulse-width > 240us) will pick it up regardless.
        if( lastLevelActive )
        {
            intervals.push_back( _us( 600.0 / spd ) ); // passive idle
            lastLevelActive = false;
        }
        else
        {
            // Insert a brief active blip we don't care about, then a long
            // passive idle. This is the safe fallback. (In practice
            // sequences end with the line at active just before EOF, so
            // this branch is rare.)
            intervals.push_back( _us( 10.0 ) );  // ignorable active glitch
            intervals.push_back( _us( 600.0 / spd ) );
            lastLevelActive = false;
        }
    }
};

void feedIntervals( MockChannelData& cd, const std::vector<double>& intervals, BitState initial )
{
    cd.TestSetInitialBitState( initial );
    double err = 0.0;
    for( double s : intervals )
        err = cd.TestAppendIntervals( kSampleRate, err, s );
    cd.ResetCurrentSample();
}

Instance* makeInstance( U32 activeLevel, bool verify = true )
{
    Instance* inst = new Instance();
    inst->CreatePlugin( "SAE J1850 VPW" );
    inst->SetSampleRate( kSampleRate );

    auto* settings = static_cast<J1850VpwAnalyzerSettings*>( inst->GetSettings() );
    settings->mInputChannel = Channel( 0, 0, DIGITAL_CHANNEL );
    settings->mActiveLevel = activeLevel;
    settings->mVerifyChecksum = verify;
    return inst;
}

struct FrameSummary
{
    int sofCount = 0;
    int eofCount = 0;
    int ifsCount = 0;
    int errorCount = 0;
    int csumOk = 0;
    int csumBad = 0;
    std::vector<U8> bytes;        // PRIO,DEST,SRC,MODE,DATA in encounter order
    std::vector<U8> csumValues;
    std::vector<int> sofSpeeds;
};

FrameSummary summarize( AnalyzerResults* res )
{
    FrameSummary s;
    // NB: AnalyzerResults::GetNumFrames() in MockResults returns size-1 (an
    // upstream off-by-one). Bypass it via MockResultData directly.
    auto* mock = MockResultData::MockFromResults( res );
    U64 n = mock->TotalFrameCount();
    for( U64 i = 0; i < n; ++i )
    {
        Frame f = mock->GetFrame( i );
        switch( f.mType )
        {
            case J1850_SOF: s.sofCount++; s.sofSpeeds.push_back( int( f.mData1 ) ); break;
            case J1850_PRIO:
            case J1850_DEST:
            case J1850_SRC:
            case J1850_MODE:
            case J1850_DATA: s.bytes.push_back( U8( f.mData1 ) ); break;
            case J1850_CSUM:
                s.csumValues.push_back( U8( f.mData1 ) );
                if( f.HasFlag( DISPLAY_AS_ERROR_FLAG ) ) s.csumBad++;
                else s.csumOk++;
                break;
            case J1850_EOF: s.eofCount++; break;
            case J1850_IFS: s.ifsCount++; break;
            case J1850_ERROR: s.errorCount++; break;
        }
    }
    return s;
}

// --------------------------------------------------------------------------
// Tests
// --------------------------------------------------------------------------

void test_sof_1x_detection()
{
    std::cout << "test_sof_1x_detection ... " << std::flush;
    Instance* inst = makeInstance( /*activeLevel=*/0 );

    FrameBuilder fb( 1 );
    fb.sof();

    auto* cd = new MockChannelData( inst );
    feedIntervals( *cd, fb.intervals, BIT_HIGH );  // passive = high
    inst->SetChannelData( Channel( 0, 0, DIGITAL_CHANNEL ), cd );

    auto rr = inst->RunAnalyzerWorker();
    TEST_VERIFY_EQ( rr, Instance::WorkerRanOutOfData );

    auto s = summarize( inst->GetResults() );
    TEST_VERIFY( s.sofCount == 1 );
    TEST_VERIFY( s.sofSpeeds[ 0 ] == 1 );
    std::cout << "ok\n";
    delete inst;
}

void test_sof_4x_detection()
{
    std::cout << "test_sof_4x_detection ... " << std::flush;
    Instance* inst = makeInstance( 0 );

    FrameBuilder fb( 4 );
    fb.sof();

    auto* cd = new MockChannelData( inst );
    feedIntervals( *cd, fb.intervals, BIT_HIGH );
    inst->SetChannelData( Channel( 0, 0, DIGITAL_CHANNEL ), cd );

    inst->RunAnalyzerWorker();
    auto s = summarize( inst->GetResults() );
    TEST_VERIFY( s.sofCount == 1 );
    TEST_VERIFY( s.sofSpeeds[ 0 ] == 4 );
    std::cout << "ok\n";
    delete inst;
}

void test_bit_classification_and_byte()
{
    std::cout << "test_bit_classification_and_byte ... " << std::flush;
    Instance* inst = makeInstance( 0 );

    // 4 header bytes + 1 data byte + CRC = 6 total.
    std::vector<U8> payload = { 0xAA, 0x55, 0xFF, 0x01, 0x42 };
    U8 c = crc8( payload );

    FrameBuilder fb( 1 );
    fb.sof();
    for( U8 b : payload ) fb.byte( b );
    fb.byte( c );
    fb.eof();

    auto* cd = new MockChannelData( inst );
    feedIntervals( *cd, fb.intervals, BIT_HIGH );
    inst->SetChannelData( Channel( 0, 0, DIGITAL_CHANNEL ), cd );

    inst->RunAnalyzerWorker();
    auto s = summarize( inst->GetResults() );

    TEST_VERIFY( s.sofCount == 1 );
    TEST_VERIFY( s.bytes.size() == 5 );  // 4 header + 1 data
    TEST_VERIFY_EQ( int( s.bytes[ 0 ] ), 0xAA );  // PRIO
    TEST_VERIFY_EQ( int( s.bytes[ 1 ] ), 0x55 );  // DEST
    TEST_VERIFY_EQ( int( s.bytes[ 2 ] ), 0xFF );  // SRC
    TEST_VERIFY_EQ( int( s.bytes[ 3 ] ), 0x01 );  // MODE
    TEST_VERIFY_EQ( int( s.bytes[ 4 ] ), 0x42 );  // DATA
    TEST_VERIFY( s.csumValues.size() == 1 );
    TEST_VERIFY_EQ( int( s.csumValues[ 0 ] ), int( c ) );
    TEST_VERIFY( s.csumOk == 1 );
    std::cout << "ok\n";
    delete inst;
}

void test_crc_pass_and_fail()
{
    std::cout << "test_crc_pass_and_fail ... " << std::flush;
    // Build a 4-byte header + correct CRC, expect OK.
    std::vector<U8> hdr = { 0x68, 0x6A, 0xF1, 0x01 };
    U8 ok_crc = crc8( hdr );
    {
        Instance* inst = makeInstance( 0 );
        FrameBuilder fb( 1 );
        fb.sof();
        for( U8 b : hdr ) fb.byte( b );
        fb.byte( ok_crc );
        fb.eof();
        auto* cd = new MockChannelData( inst );
        feedIntervals( *cd, fb.intervals, BIT_HIGH );
        inst->SetChannelData( Channel( 0, 0, DIGITAL_CHANNEL ), cd );
        inst->RunAnalyzerWorker();
        auto s = summarize( inst->GetResults() );
        TEST_VERIFY( s.csumOk == 1 );
        TEST_VERIFY( s.csumBad == 0 );
        delete inst;
    }

    // Now corrupt one byte and verify the CSUM frame gets the error flag.
    {
        Instance* inst = makeInstance( 0 );
        FrameBuilder fb( 1 );
        fb.sof();
        for( U8 b : hdr ) fb.byte( b );
        fb.byte( ok_crc ^ 0xFF );   // wrong checksum
        fb.eof();
        auto* cd = new MockChannelData( inst );
        feedIntervals( *cd, fb.intervals, BIT_HIGH );
        inst->SetChannelData( Channel( 0, 0, DIGITAL_CHANNEL ), cd );
        inst->RunAnalyzerWorker();
        auto s = summarize( inst->GetResults() );
        TEST_VERIFY( s.csumOk == 0 );
        TEST_VERIFY( s.csumBad == 1 );
        delete inst;
    }

    std::cout << "ok\n";
}

void test_polarity_inversion()
{
    std::cout << "test_polarity_inversion ... " << std::flush;
    // The decoder should treat "active high" exactly the same as "active low"
    // for bit decoding, as long as the initial idle state matches.
    Instance* inst = makeInstance( /*activeLevel=*/1 );
    FrameBuilder fb( 1 );
    fb.sof();
    fb.byte( 0xAA );
    fb.byte( 0x55 );
    fb.byte( 0x00 );
    fb.byte( 0xCC );
    fb.eof();
    auto* cd = new MockChannelData( inst );
    feedIntervals( *cd, fb.intervals, BIT_LOW );   // passive = low (since active is now high)
    inst->SetChannelData( Channel( 0, 0, DIGITAL_CHANNEL ), cd );
    inst->RunAnalyzerWorker();
    auto s = summarize( inst->GetResults() );
    TEST_VERIFY( s.sofCount == 1 );
    TEST_VERIFY_EQ( int( s.bytes[ 0 ] ), 0xAA );
    TEST_VERIFY_EQ( int( s.bytes[ 1 ] ), 0x55 );
    TEST_VERIFY_EQ( int( s.bytes[ 2 ] ), 0x00 );
    std::cout << "ok\n";
    delete inst;
}

void test_eof_recognized()
{
    std::cout << "test_eof_recognized ... " << std::flush;
    Instance* inst = makeInstance( 0 );
    std::vector<U8> hdr = { 0x68, 0x6A, 0xF1, 0x01 };
    U8 c = crc8( hdr );

    FrameBuilder fb( 1 );
    fb.sof();
    for( U8 b : hdr ) fb.byte( b );
    fb.byte( c );
    fb.eof();
    auto* cd = new MockChannelData( inst );
    feedIntervals( *cd, fb.intervals, BIT_HIGH );
    inst->SetChannelData( Channel( 0, 0, DIGITAL_CHANNEL ), cd );
    inst->RunAnalyzerWorker();
    auto s = summarize( inst->GetResults() );
    TEST_VERIFY( s.eofCount + s.ifsCount >= 1 );
    std::cout << "ok\n";
    delete inst;
}

}  // namespace

int main( int /*argc*/, char** /*argv*/ )
{
    try
    {
        test_sof_1x_detection();
        test_sof_4x_detection();
        test_bit_classification_and_byte();
        test_crc_pass_and_fail();
        test_polarity_inversion();
        test_eof_recognized();
    }
    catch( std::exception& e )
    {
        std::cerr << "test failed: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "all synthetic tests passed\n";
    return EXIT_SUCCESS;
}
