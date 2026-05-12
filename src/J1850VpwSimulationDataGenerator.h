#ifndef J1850VPW_SIMULATION_DATA_GENERATOR_H
#define J1850VPW_SIMULATION_DATA_GENERATOR_H

#include <SimulationChannelDescriptor.h>

class J1850VpwAnalyzerSettings;

class J1850VpwSimulationDataGenerator
{
  public:
    J1850VpwSimulationDataGenerator();
    ~J1850VpwSimulationDataGenerator();

    void Initialize( U32 simulation_sample_rate, J1850VpwAnalyzerSettings* settings );
    U32 GenerateSimulationData( U64 newest_sample_requested, U32 sample_rate, SimulationChannelDescriptor** simulation_channel );

  protected:
    J1850VpwAnalyzerSettings* mSettings;
    U32 mSimulationSampleRateHz;
    SimulationChannelDescriptor mChannel;
    U32 mFrameCounter;

    BitState mActiveLevel;
    BitState mPassiveLevel;
    BitState mCurrentLevel;     // running level we hold the line at

    void AdvanceUs( double us );
    void EmitPulse( double us, BitState level );
    void EmitSof( int spd );
    void EmitBit( int spd, U8 bit );
    void EmitByte( int spd, U8 byte );
    void EmitEof( int spd );
    void EmitMessage( int spd, const U8* bytes, int n_header_and_data );

    static U8 ComputeCrc( const U8* data, int n );
};

#endif
