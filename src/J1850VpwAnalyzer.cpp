#include "J1850VpwAnalyzer.h"

#include <AnalyzerChannelData.h>
#include <AnalyzerHelpers.h>

#include <cmath>
#include <cstdio>

J1850VpwAnalyzer::J1850VpwAnalyzer()
    : Analyzer2(),
      mSettings(),
      mChannel( nullptr ),
      mSimulationInitialized( false ),
      mByteStartSample( 0 ),
      mSpeed( 0 ),
      mShortLo( 0 ), mShortHi( 0 ), mLongLo( 0 ), mLongHi( 0 ), mIfsUs( 0 ),
      mActive( BIT_LOW ), mPassive( BIT_HIGH ),
      mUsPerSample( 0.0 )
{
    SetAnalyzerSettings( &mSettings );
    UseFrameV2();
}

J1850VpwAnalyzer::~J1850VpwAnalyzer()
{
    KillThread();
}

void J1850VpwAnalyzer::SetupResults()
{
    mResults.reset( new J1850VpwAnalyzerResults( this, &mSettings ) );
    SetAnalyzerResults( mResults.get() );
    mResults->AddChannelBubblesWillAppearOn( mSettings.mInputChannel );
}

U32 J1850VpwAnalyzer::GenerateSimulationData( U64 newest_sample_requested, U32 sample_rate, SimulationChannelDescriptor** simulation_channels )
{
    if( !mSimulationInitialized )
    {
        mSimulationDataGenerator.Initialize( GetSimulationSampleRate(), &mSettings );
        mSimulationInitialized = true;
    }
    return mSimulationDataGenerator.GenerateSimulationData( newest_sample_requested, sample_rate, simulation_channels );
}

U32 J1850VpwAnalyzer::GetMinimumSampleRateHz()
{
    // J1850 1x short pulses can be as small as 24us (with practical tolerance).
    // 1 MHz gives ~24 samples per shortest pulse — comfortable margin.
    return 1000000;
}

const char* J1850VpwAnalyzer::GetAnalyzerName() const { return "SAE J1850 VPW"; }

bool J1850VpwAnalyzer::NeedsRerun() { return false; }

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

void J1850VpwAnalyzer::resetFrameState()
{
    mBitAccum.clear();
    mBytes.clear();
    mByteStartSample = 0;
    mSpeed = 0;
}

U8 J1850VpwAnalyzer::j1850Crc( const U8* data, int n )
{
    U8 crc = 0xFF;
    for( int i = 0; i < n; ++i )
    {
        crc ^= data[ i ];
        for( int b = 0; b < 8; ++b )
            crc = ( crc & 0x80 ) ? static_cast<U8>( ( crc << 1 ) ^ 0x1D ) : static_cast<U8>( crc << 1 );
    }
    return static_cast<U8>( crc ^ 0xFF );
}

void J1850VpwAnalyzer::pushBit( U8 b, U64 ss, U64 es )
{
    if( mBitAccum.empty() )
        mByteStartSample = ss;
    mBitAccum.push_back( { b, es } );

    mResults->AddMarker( ss + ( es - ss ) / 2, AnalyzerResults::Dot, mSettings.mInputChannel );

    if( mBitAccum.size() == 8 )
    {
        U8 v = 0;
        for( int i = 0; i < 8; ++i )
            v = static_cast<U8>( ( v << 1 ) | ( mBitAccum[ i ].first & 1u ) );
        U64 byteEnd = mBitAccum.back().second;
        emitByte( v, mByteStartSample, byteEnd );
        mBitAccum.clear();
    }
}

void J1850VpwAnalyzer::emitByte( U8 v, U64 ss, U64 es )
{
    int idx = static_cast<int>( mBytes.size() );
    mBytes.push_back( { v, ss, es } );

    // Header fields (bytes 0..3) are always one of PRIO/DEST/SRC/MODE — emit
    // their frames immediately for live streaming. Data and CSUM bytes are
    // emitted later at EOF, when we know which one is the CRC.
    U8 type = 0xFF;
    switch( idx )
    {
        case 0: type = J1850_PRIO; break;
        case 1: type = J1850_DEST; break;
        case 2: type = J1850_SRC; break;
        case 3: type = J1850_MODE; break;
        default: return;  // defer; emitted in emitEof
    }

    Frame f;
    f.mStartingSampleInclusive = static_cast<S64>( ss );
    f.mEndingSampleInclusive = static_cast<S64>( es );
    f.mData1 = v;
    f.mData2 = static_cast<U64>( idx );
    f.mType = type;
    f.mFlags = 0;
    mResults->AddFrame( f );

#ifdef LOGIC2
    FrameV2 fv2;
    fv2.AddByte( "value", v );
    fv2.AddInteger( "index", idx );
    mResults->AddFrameV2( fv2, "byte", ss, es );
#endif

    mResults->CommitResults();
}

void J1850VpwAnalyzer::emitSof( U64 ss, U64 es, int spd )
{
    Frame f;
    f.mStartingSampleInclusive = static_cast<S64>( ss );
    f.mEndingSampleInclusive = static_cast<S64>( es );
    f.mData1 = static_cast<U64>( spd );
    f.mData2 = 0;
    f.mType = J1850_SOF;
    f.mFlags = 0;
    mResults->AddFrame( f );

#ifdef LOGIC2
    FrameV2 fv2;
    fv2.AddInteger( "speed", spd );
    mResults->AddFrameV2( fv2, "sof", ss, es );
#endif

    mResults->CommitResults();
}

void J1850VpwAnalyzer::emitEof( U64 ss, U64 es, bool isIfs )
{
    // Drop any partial accumulated bits — they were not closed by a byte.
    mBitAccum.clear();

    // Header bytes (0..3) were already emitted as PRIO/DEST/SRC/MODE.
    // Bytes 4..n-1 are buffered in mBytes; the last one is the CRC (CSUM),
    // any preceding ones are DATA. A frame shorter than 5 bytes is malformed
    // and produces no CSUM frame.
    bool haveCsum = mBytes.size() >= 5;
    U8 csum = 0;
    U64 csum_ss = 0, csum_es = 0;
    int firstData = 4;
    int lastData = static_cast<int>( mBytes.size() ) - 1;  // index of CRC byte
    if( haveCsum )
    {
        const auto& last = mBytes[ static_cast<size_t>( lastData ) ];
        csum = last.value;
        csum_ss = last.ss;
        csum_es = last.es;

        // Emit individual DATA frames for bytes 4..lastData-1.
        for( int i = firstData; i < lastData; ++i )
        {
            const auto& br = mBytes[ static_cast<size_t>( i ) ];
            Frame d;
            d.mStartingSampleInclusive = static_cast<S64>( br.ss );
            d.mEndingSampleInclusive = static_cast<S64>( br.es );
            d.mData1 = br.value;
            d.mData2 = static_cast<U64>( i );
            d.mType = J1850_DATA;
            d.mFlags = 0;
            mResults->AddFrame( d );
#ifdef LOGIC2
            FrameV2 dv;
            dv.AddByte( "value", br.value );
            dv.AddInteger( "index", i );
            mResults->AddFrameV2( dv, "byte", br.ss, br.es );
#endif
        }
    }

    // Compute CRC over header + data (everything except CSUM).
    std::vector<U8> hdrAndData;
    int upTo = haveCsum ? lastData : static_cast<int>( mBytes.size() );
    hdrAndData.reserve( static_cast<size_t>( upTo ) );
    for( int i = 0; i < upTo; ++i )
        hdrAndData.push_back( mBytes[ static_cast<size_t>( i ) ].value );

    U8 calc = j1850Crc( hdrAndData.data(), static_cast<int>( hdrAndData.size() ) );
    bool csum_ok = haveCsum && ( calc == csum );

    if( haveCsum )
    {
        Frame f;
        f.mStartingSampleInclusive = static_cast<S64>( csum_ss );
        f.mEndingSampleInclusive = static_cast<S64>( csum_es );
        f.mData1 = csum;
        f.mData2 = calc;
        f.mType = J1850_CSUM;
        f.mFlags = ( mSettings.mVerifyChecksum && !csum_ok ) ? DISPLAY_AS_ERROR_FLAG : 0;
        mResults->AddFrame( f );

#ifdef LOGIC2
        FrameV2 fv2;
        fv2.AddByte( "csum", csum );
        fv2.AddByte( "calc", calc );
        fv2.AddBoolean( "ok", csum_ok );
        mResults->AddFrameV2( fv2, "csum", csum_ss, csum_es );
#endif
    }

    // EOF / IFS marker frame.
    Frame e;
    e.mStartingSampleInclusive = static_cast<S64>( ss );
    e.mEndingSampleInclusive = static_cast<S64>( es );
    e.mData1 = 0;
    e.mData2 = 0;
    e.mType = isIfs ? J1850_IFS : J1850_EOF;
    e.mFlags = 0;
    mResults->AddFrame( e );

#ifdef LOGIC2
    {
        // Emit a per-message FrameV2 spanning the full frame.
        // Note: header fields are only valid if we saw at least 4 bytes.
        FrameV2 fv2;
        fv2.AddInteger( "speed", mSpeed );
        if( hdrAndData.size() >= 1 ) fv2.AddByte( "priority", hdrAndData[ 0 ] );
        if( hdrAndData.size() >= 2 ) fv2.AddByte( "dest", hdrAndData[ 1 ] );
        if( hdrAndData.size() >= 3 ) fv2.AddByte( "src", hdrAndData[ 2 ] );
        if( hdrAndData.size() >= 4 ) fv2.AddByte( "mode", hdrAndData[ 3 ] );
        if( hdrAndData.size() > 4 )
            fv2.AddByteArray( "data", hdrAndData.data() + 4, hdrAndData.size() - 4 );
        if( haveCsum )
        {
            fv2.AddByte( "csum", csum );
            fv2.AddBoolean( "csum_ok", csum_ok );
        }
        U64 msg_start = mBytes.empty() ? ( haveCsum ? csum_ss : ss ) : mBytes.front().ss;
        mResults->AddFrameV2( fv2, "message", msg_start, es );
    }
#endif

    mResults->CommitResults();
}

void J1850VpwAnalyzer::emitError( U64 ss, U64 es, double us )
{
    Frame f;
    f.mStartingSampleInclusive = static_cast<S64>( ss );
    f.mEndingSampleInclusive = static_cast<S64>( es );
    f.mData1 = static_cast<U64>( us );
    f.mData2 = 0;
    f.mType = J1850_ERROR;
    f.mFlags = DISPLAY_AS_ERROR_FLAG;
    mResults->AddFrame( f );

#ifdef LOGIC2
    FrameV2 fv2;
    fv2.AddDouble( "width_us", us );
    mResults->AddFrameV2( fv2, "error", ss, es );
#endif

    mResults->CommitResults();
}

// -----------------------------------------------------------------------------
// Worker thread
// -----------------------------------------------------------------------------

void J1850VpwAnalyzer::WorkerThread()
{
    U32 sample_rate_hz = GetSampleRate();
    mUsPerSample = 1e6 / static_cast<double>( sample_rate_hz );

    mActive = mSettings.mActiveLevel ? BIT_HIGH : BIT_LOW;
    mPassive = mActive == BIT_HIGH ? BIT_LOW : BIT_HIGH;

    mChannel = GetAnalyzerChannelData( mSettings.mInputChannel );

    // Sync to passive idle: if the line is currently active, ride until it goes passive.
    if( mChannel->GetBitState() == mActive )
        mChannel->AdvanceToNextEdge();

    U64 ss = mChannel->GetSampleNumber();
    resetFrameState();

    for( ;; )
    {
        // While inside a frame, watch for an inter-frame idle that exceeds the
        // pre-computed EOF threshold; in that case synthesize an EOF event
        // before the next edge actually arrives.
        if( mSpeed != 0 )
        {
            BitState here = mChannel->GetBitState();
            if( here == mPassive )
            {
                U64 nextEdge = mChannel->GetSampleOfNextEdge();
                double dt = widthUs( ss, nextEdge );
                if( dt > mIfsUs )
                {
                    U64 eofEnd = ss + static_cast<U64>( mIfsUs / mUsPerSample );
                    if( eofEnd > nextEdge ) eofEnd = nextEdge;
                    bool isIfs = dt > ( 280.0 / mSpeed );
                    emitEof( ss, eofEnd, isIfs );
                    // jump to the idle endpoint
                    mChannel->AdvanceToAbsPosition( eofEnd );
                    ss = eofEnd;
                    mSpeed = 0;
                    resetFrameState();
                    ReportProgress( ss );
                    continue;
                }
            }
        }

        mChannel->AdvanceToNextEdge();
        U64 es = mChannel->GetSampleNumber();
        BitState pin = mChannel->GetBitState();          // level AFTER the edge
        BitState prev = pin == BIT_HIGH ? BIT_LOW : BIT_HIGH;
        double dt = widthUs( ss, es );

        if( mSpeed == 0 )
        {
            // Need a SOF: an active-level pulse of SOF width.
            if( prev != mActive ) { ss = es; continue; }
            int spd = 0;
            if( dt >= 164.0 && dt <= 245.0 ) spd = 1;
            else if( dt >= 41.0 && dt <= 62.0 ) spd = 4;     // 164/4 .. 245/4 (rounded)
            if( spd == 0 ) { ss = es; continue; }

            mSpeed = spd;
            mShortLo = 24.0 / spd; mShortHi = 97.0 / spd;
            mLongLo  = 97.0 / spd; mLongHi  = 170.0 / spd;
            mIfsUs   = 240.0 / spd;
            emitSof( ss, es, spd );
            ReportProgress( es );
            ss = es;
            continue;
        }

        // Inside a frame: classify the pulse just observed.
        // SAE J1850 VPW bit encoding:
        //   active  short = 1     active  long = 0
        //   passive short = 0     passive long = 1
        if( dt >= mShortLo && dt <= mShortHi )
        {
            U8 b = ( prev == mActive ) ? 1u : 0u;
            pushBit( b, ss, es );
        }
        else if( dt >= mLongLo && dt <= mLongHi )
        {
            U8 b = ( prev == mActive ) ? 0u : 1u;
            pushBit( b, ss, es );
        }
        else if( dt > mIfsUs )
        {
            // Long passive (or long active) — treat as EOF/IFS.
            bool isIfs = dt > ( 280.0 / mSpeed );
            emitEof( ss, es, isIfs );
            mSpeed = 0;
            resetFrameState();
        }
        else if( dt <= 2.0 )
        {
            // Glitch — ignore. Sub-microsecond edges are below the noise floor
            // of typical VPW transceivers.
        }
        else
        {
            emitError( ss, es, dt );
            mSpeed = 0;
            resetFrameState();
        }

        ReportProgress( es );
        ss = es;
    }
}

// -----------------------------------------------------------------------------
// C-linkage exports
// -----------------------------------------------------------------------------

const char* GetAnalyzerName()
{
    return "SAE J1850 VPW";
}

Analyzer* CreateAnalyzer()
{
    return new J1850VpwAnalyzer();
}

void DestroyAnalyzer( Analyzer* analyzer )
{
    delete analyzer;
}
