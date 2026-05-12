#include "J1850VpwSimulationDataGenerator.h"
#include "J1850VpwAnalyzerSettings.h"

#include <AnalyzerHelpers.h>

J1850VpwSimulationDataGenerator::J1850VpwSimulationDataGenerator()
    : mSettings( nullptr ),
      mSimulationSampleRateHz( 0 ),
      mFrameCounter( 0 ),
      mActiveLevel( BIT_LOW ),
      mPassiveLevel( BIT_HIGH ),
      mCurrentLevel( BIT_HIGH )
{
}

J1850VpwSimulationDataGenerator::~J1850VpwSimulationDataGenerator() {}

void J1850VpwSimulationDataGenerator::Initialize( U32 simulation_sample_rate, J1850VpwAnalyzerSettings* settings )
{
    mSimulationSampleRateHz = simulation_sample_rate;
    mSettings = settings;

    mActiveLevel = settings->mActiveLevel ? BIT_HIGH : BIT_LOW;
    mPassiveLevel = mActiveLevel == BIT_HIGH ? BIT_LOW : BIT_HIGH;
    mCurrentLevel = mPassiveLevel;

    mChannel.SetChannel( settings->mInputChannel );
    mChannel.SetSampleRate( simulation_sample_rate );
    mChannel.SetInitialBitState( mPassiveLevel );

    // Start with a small idle gap.
    AdvanceUs( 500.0 );
}

void J1850VpwSimulationDataGenerator::AdvanceUs( double us )
{
    if( us <= 0.0 ) return;
    U32 samples = static_cast<U32>( ( us * 1e-6 ) * static_cast<double>( mSimulationSampleRateHz ) );
    if( samples == 0 ) samples = 1;
    mChannel.Advance( samples );
}

void J1850VpwSimulationDataGenerator::EmitPulse( double us, BitState level )
{
    if( mCurrentLevel != level )
    {
        mChannel.Transition();
        mCurrentLevel = level;
    }
    AdvanceUs( us );
}

void J1850VpwSimulationDataGenerator::EmitSof( int spd )
{
    EmitPulse( 200.0 / spd, mActiveLevel );
}

// SAE J1850 VPW bit encoding:
//   active short  = 1
//   active long   = 0
//   passive short = 0
//   passive long  = 1
//
// During DATA the line alternates between active and passive between bits.
void J1850VpwSimulationDataGenerator::EmitBit( int spd, U8 bit )
{
    BitState next = ( mCurrentLevel == mActiveLevel ) ? mPassiveLevel : mActiveLevel;
    double width_us;
    if( next == mActiveLevel )
    {
        // active short = 1, active long = 0
        width_us = bit ? ( 64.0 / spd ) : ( 128.0 / spd );
    }
    else
    {
        // passive short = 0, passive long = 1
        width_us = bit ? ( 128.0 / spd ) : ( 64.0 / spd );
    }
    EmitPulse( width_us, next );
}

void J1850VpwSimulationDataGenerator::EmitByte( int spd, U8 byte )
{
    for( int i = 7; i >= 0; --i )
        EmitBit( spd, ( byte >> i ) & 1u );
}

void J1850VpwSimulationDataGenerator::EmitEof( int spd )
{
    // Drive passive for an EOF-length idle.
    if( mCurrentLevel != mPassiveLevel )
    {
        mChannel.Transition();
        mCurrentLevel = mPassiveLevel;
    }
    AdvanceUs( 300.0 / spd );
}

U8 J1850VpwSimulationDataGenerator::ComputeCrc( const U8* data, int n )
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

void J1850VpwSimulationDataGenerator::EmitMessage( int spd, const U8* bytes, int n )
{
    EmitSof( spd );
    for( int i = 0; i < n; ++i )
        EmitByte( spd, bytes[ i ] );
    U8 crc = ComputeCrc( bytes, n );
    EmitByte( spd, crc );
    EmitEof( spd );
}

U32 J1850VpwSimulationDataGenerator::GenerateSimulationData( U64 largest_sample_requested, U32 sample_rate, SimulationChannelDescriptor** simulation_channel )
{
    U64 target = AnalyzerHelpers::AdjustSimulationTargetSample( largest_sample_requested, sample_rate, mSimulationSampleRateHz );

    static const U8 msg_a[] = { 0x68, 0x6A, 0xF1, 0x01 };               // request mode 1 PID
    static const U8 msg_b[] = { 0x48, 0x6B, 0x10, 0x41, 0x00, 0xBE };   // response

    while( mChannel.GetCurrentSampleNumber() < target )
    {
        int spd = ( mFrameCounter % 4 == 3 ) ? 4 : 1;
        if( mFrameCounter % 2 == 0 )
            EmitMessage( spd, msg_a, sizeof msg_a );
        else
            EmitMessage( spd, msg_b, sizeof msg_b );
        AdvanceUs( 800.0 );  // longer inter-frame idle
        ++mFrameCounter;
    }

    *simulation_channel = &mChannel;
    return 1;
}
