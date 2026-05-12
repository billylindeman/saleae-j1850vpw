#ifndef J1850VPW_ANALYZER_RESULTS_H
#define J1850VPW_ANALYZER_RESULTS_H

#include <AnalyzerResults.h>

class J1850VpwAnalyzer;
class J1850VpwAnalyzerSettings;

// Frame type enumeration. mType is U8 in AnalyzerResults.h.
enum J1850FrameType : U8
{
    J1850_SOF   = 0,
    J1850_PRIO  = 1,
    J1850_DEST  = 2,
    J1850_SRC   = 3,
    J1850_MODE  = 4,
    J1850_DATA  = 5,
    J1850_CSUM  = 6,
    J1850_EOF   = 7,
    J1850_IFS   = 8,
    J1850_NORM  = 9,
    J1850_ERROR = 10,
};

class J1850VpwAnalyzerResults : public AnalyzerResults
{
  public:
    J1850VpwAnalyzerResults( J1850VpwAnalyzer* analyzer, J1850VpwAnalyzerSettings* settings );
    virtual ~J1850VpwAnalyzerResults();

    virtual void GenerateBubbleText( U64 frame_index, Channel& channel, DisplayBase display_base );
    virtual void GenerateExportFile( const char* file, DisplayBase display_base, U32 export_type_user_id );
    virtual void GenerateFrameTabularText( U64 frame_index, DisplayBase display_base );
    virtual void GeneratePacketTabularText( U64 packet_id, DisplayBase display_base );
    virtual void GenerateTransactionTabularText( U64 transaction_id, DisplayBase display_base );

  protected:
    J1850VpwAnalyzerSettings* mSettings;
    J1850VpwAnalyzer* mAnalyzer;
};

#endif
