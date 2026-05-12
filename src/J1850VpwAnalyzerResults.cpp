#include "J1850VpwAnalyzerResults.h"
#include "J1850VpwAnalyzer.h"
#include "J1850VpwAnalyzerSettings.h"

#include <AnalyzerHelpers.h>

#include <cstdio>
#include <fstream>
#include <iostream>

J1850VpwAnalyzerResults::J1850VpwAnalyzerResults( J1850VpwAnalyzer* analyzer, J1850VpwAnalyzerSettings* settings )
    : AnalyzerResults(), mSettings( settings ), mAnalyzer( analyzer )
{
}

J1850VpwAnalyzerResults::~J1850VpwAnalyzerResults() {}

static void formatByte( U64 v, DisplayBase base, char* out, U32 cap )
{
    AnalyzerHelpers::GetNumberString( v, base, 8, out, cap );
}

void J1850VpwAnalyzerResults::GenerateBubbleText( U64 frame_index, Channel& /*channel*/, DisplayBase display_base )
{
    ClearResultStrings();
    Frame frame = GetFrame( frame_index );

    char num[ 32 ];
    char buf[ 192 ];

    switch( frame.mType )
    {
        case J1850_SOF:
        {
            AddResultString( "S" );
            AddResultString( "SOF" );
            std::snprintf( buf, sizeof buf, "SOF %ux", static_cast<unsigned>( frame.mData1 ) );
            AddResultString( buf );
            break;
        }
        case J1850_PRIO:
            formatByte( frame.mData1, display_base, num, sizeof num );
            AddResultString( "P" );
            AddResultString( "Prio" );
            std::snprintf( buf, sizeof buf, "Prio %s", num );
            AddResultString( buf );
            break;
        case J1850_DEST:
            formatByte( frame.mData1, display_base, num, sizeof num );
            AddResultString( "D" );
            AddResultString( "Dest" );
            std::snprintf( buf, sizeof buf, "Dest %s", num );
            AddResultString( buf );
            break;
        case J1850_SRC:
            formatByte( frame.mData1, display_base, num, sizeof num );
            AddResultString( "Sr" );
            AddResultString( "Src" );
            std::snprintf( buf, sizeof buf, "Src %s", num );
            AddResultString( buf );
            break;
        case J1850_MODE:
            formatByte( frame.mData1, display_base, num, sizeof num );
            AddResultString( "M" );
            AddResultString( "Mode" );
            std::snprintf( buf, sizeof buf, "Mode %s", num );
            AddResultString( buf );
            break;
        case J1850_DATA:
            formatByte( frame.mData1, display_base, num, sizeof num );
            AddResultString( num );
            std::snprintf( buf, sizeof buf, "Data %s", num );
            AddResultString( buf );
            break;
        case J1850_CSUM:
        {
            formatByte( frame.mData1, display_base, num, sizeof num );
            bool bad = frame.HasFlag( DISPLAY_AS_ERROR_FLAG );
            if( bad )
            {
                char calc[ 64 ];
                formatByte( frame.mData2, display_base, calc, sizeof calc );
                AddResultString( "!CS" );
                std::snprintf( buf, sizeof buf, "BAD CSUM %s", num );
                AddResultString( buf );
                std::snprintf( buf, sizeof buf, "BAD CSUM %s (calc %s)", num, calc );
                AddResultString( buf );
            }
            else
            {
                AddResultString( "CS" );
                std::snprintf( buf, sizeof buf, "CSUM %s", num );
                AddResultString( buf );
            }
            break;
        }
        case J1850_EOF:
            AddResultString( "E" );
            AddResultString( "EOF" );
            break;
        case J1850_IFS:
            AddResultString( "I" );
            AddResultString( "IFS" );
            break;
        case J1850_NORM:
            AddResultString( "N" );
            AddResultString( "Norm" );
            AddResultString( "Norm Bit" );
            break;
        case J1850_ERROR:
        default:
            AddResultString( "!" );
            AddResultString( "ERR" );
            std::snprintf( buf, sizeof buf, "ERR %u us", static_cast<unsigned>( frame.mData1 ) );
            AddResultString( buf );
            break;
    }
}

static const char* typeName( U8 t )
{
    switch( t )
    {
        case J1850_SOF: return "SOF";
        case J1850_PRIO: return "Prio";
        case J1850_DEST: return "Dest";
        case J1850_SRC: return "Src";
        case J1850_MODE: return "Mode";
        case J1850_DATA: return "Data";
        case J1850_CSUM: return "Csum";
        case J1850_EOF: return "EOF";
        case J1850_IFS: return "IFS";
        case J1850_NORM: return "Norm";
        case J1850_ERROR: return "Error";
        default: return "?";
    }
}

void J1850VpwAnalyzerResults::GenerateExportFile( const char* file, DisplayBase display_base, U32 /*export_type_user_id*/ )
{
    std::ofstream out( file, std::ios::out );

    U64 trigger_sample = mAnalyzer->GetTriggerSample();
    U32 sample_rate = mAnalyzer->GetSampleRate();

    out << "Time [s],Type,Value,Note\n";

    U64 num = GetNumFrames();
    for( U64 i = 0; i < num; ++i )
    {
        Frame f = GetFrame( i );

        char time_str[ 64 ];
        AnalyzerHelpers::GetTimeString( f.mStartingSampleInclusive, trigger_sample, sample_rate, time_str, sizeof time_str );

        char val_str[ 64 ] = { 0 };
        char note[ 96 ] = { 0 };

        switch( f.mType )
        {
            case J1850_SOF:
                std::snprintf( val_str, sizeof val_str, "%ux", static_cast<unsigned>( f.mData1 ) );
                break;
            case J1850_PRIO:
            case J1850_DEST:
            case J1850_SRC:
            case J1850_MODE:
            case J1850_DATA:
                AnalyzerHelpers::GetNumberString( f.mData1, display_base, 8, val_str, sizeof val_str );
                break;
            case J1850_CSUM:
            {
                AnalyzerHelpers::GetNumberString( f.mData1, display_base, 8, val_str, sizeof val_str );
                if( f.HasFlag( DISPLAY_AS_ERROR_FLAG ) )
                {
                    char calc[ 64 ];
                    AnalyzerHelpers::GetNumberString( f.mData2, display_base, 8, calc, sizeof calc );
                    std::snprintf( note, sizeof note, "BAD (calc %s)", calc );
                }
                else
                {
                    std::snprintf( note, sizeof note, "OK" );
                }
                break;
            }
            case J1850_ERROR:
                std::snprintf( val_str, sizeof val_str, "%u us", static_cast<unsigned>( f.mData1 ) );
                break;
            default:
                break;
        }

        out << time_str << "," << typeName( f.mType ) << "," << val_str << "," << note << "\n";

        if( UpdateExportProgressAndCheckForCancel( i, num ) )
        {
            out.close();
            return;
        }
    }
    out.close();
}

void J1850VpwAnalyzerResults::GenerateFrameTabularText( U64 frame_index, DisplayBase display_base )
{
#ifdef SUPPORTS_PROTOCOL_SEARCH
    Frame f = GetFrame( frame_index );
    ClearTabularText();

    char num[ 64 ];
    char buf[ 128 ];
    switch( f.mType )
    {
        case J1850_SOF:
            std::snprintf( buf, sizeof buf, "SOF %ux", static_cast<unsigned>( f.mData1 ) );
            AddTabularText( buf );
            break;
        case J1850_PRIO:
        case J1850_DEST:
        case J1850_SRC:
        case J1850_MODE:
        case J1850_DATA:
            AnalyzerHelpers::GetNumberString( f.mData1, display_base, 8, num, sizeof num );
            std::snprintf( buf, sizeof buf, "%s %s", typeName( f.mType ), num );
            AddTabularText( buf );
            break;
        case J1850_CSUM:
            AnalyzerHelpers::GetNumberString( f.mData1, display_base, 8, num, sizeof num );
            if( f.HasFlag( DISPLAY_AS_ERROR_FLAG ) )
                std::snprintf( buf, sizeof buf, "BAD CSUM %s", num );
            else
                std::snprintf( buf, sizeof buf, "CSUM %s", num );
            AddTabularText( buf );
            break;
        case J1850_EOF:
            AddTabularText( "EOF" );
            break;
        case J1850_IFS:
            AddTabularText( "IFS" );
            break;
        case J1850_NORM:
            AddTabularText( "Norm" );
            break;
        case J1850_ERROR:
            std::snprintf( buf, sizeof buf, "ERR %u us", static_cast<unsigned>( f.mData1 ) );
            AddTabularText( buf );
            break;
        default:
            AddTabularText( "?" );
            break;
    }
#else
    (void)frame_index;
    (void)display_base;
#endif
}

void J1850VpwAnalyzerResults::GeneratePacketTabularText( U64 /*packet_id*/, DisplayBase /*display_base*/ )
{
    // not supported
}

void J1850VpwAnalyzerResults::GenerateTransactionTabularText( U64 /*transaction_id*/, DisplayBase /*display_base*/ )
{
    // not supported
}
