#ifndef J1850VPW_ANALYZER_H
#define J1850VPW_ANALYZER_H

#include <Analyzer.h>

#include "J1850VpwAnalyzerResults.h"
#include "J1850VpwAnalyzerSettings.h"
#include "J1850VpwSimulationDataGenerator.h"

#include <memory>
#include <vector>

class ANALYZER_EXPORT J1850VpwAnalyzer : public Analyzer2
{
  public:
    J1850VpwAnalyzer();
    virtual ~J1850VpwAnalyzer();

    virtual void SetupResults();
    virtual void WorkerThread();

    virtual U32 GenerateSimulationData( U64 newest_sample_requested, U32 sample_rate, SimulationChannelDescriptor** simulation_channels );
    virtual U32 GetMinimumSampleRateHz();

    virtual const char* GetAnalyzerName() const;
    virtual bool NeedsRerun();

  protected:
    // settings, owned by us
    J1850VpwAnalyzerSettings mSettings;
    std::unique_ptr<J1850VpwAnalyzerResults> mResults;

    AnalyzerChannelData* mChannel;

    J1850VpwSimulationDataGenerator mSimulationDataGenerator;
    bool mSimulationInitialized;

    // ----- decode state, scoped to a single frame -----
    struct ByteRec
    {
        U8 value;
        U64 ss;
        U64 es;
    };
    std::vector<ByteRec> mBytes;
    std::vector<std::pair<U8, U64>> mBitAccum; // {value, end-sample-of-bit}
    U64 mByteStartSample;
    int mSpeed;            // 0 idle, 1 or 4 locked
    double mShortLo, mShortHi, mLongLo, mLongHi, mIfsUs;
    BitState mActive;
    BitState mPassive;
    double mUsPerSample;

    void resetFrameState();
    void pushBit( U8 b, U64 ss, U64 es );
    void emitByte( U8 v, U64 ss, U64 es );
    void emitSof( U64 ss, U64 es, int spd );
    void emitEof( U64 ss, U64 es, bool isIfs );
    void emitError( U64 ss, U64 es, double us );

    static U8 j1850Crc( const U8* data, int n );

    double widthUs( U64 a, U64 b ) const { return ( b - a ) * mUsPerSample; }
};

extern "C" ANALYZER_EXPORT const char* __cdecl GetAnalyzerName();
extern "C" ANALYZER_EXPORT Analyzer* __cdecl CreateAnalyzer();
extern "C" ANALYZER_EXPORT void __cdecl DestroyAnalyzer( Analyzer* analyzer );

#endif
