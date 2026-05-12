#include "J1850VpwAnalyzerSettings.h"
#include <AnalyzerHelpers.h>

J1850VpwAnalyzerSettings::J1850VpwAnalyzerSettings()
    : mInputChannel( UNDEFINED_CHANNEL ),
      mActiveLevel( 0 ),
      mVerifyChecksum( true )
{
    mInputChannelInterface.SetTitleAndTooltip( "VPW Data", "SAE J1850 VPW data line" );
    mInputChannelInterface.SetChannel( mInputChannel );

    mActiveLevelInterface.SetTitleAndTooltip(
        "Active Level",
        "Logic level that represents the active (dominant) bus state. "
        "Active Low matches transceiver outputs; Active High matches a raw VPW bus." );
    mActiveLevelInterface.AddNumber( 0.0, "Active Low", "Active state = 0 (typical transceiver output)" );
    mActiveLevelInterface.AddNumber( 1.0, "Active High", "Active state = 1 (raw bus probing)" );
    mActiveLevelInterface.SetNumber( static_cast<double>( mActiveLevel ) );

    mVerifyChecksumInterface.SetTitleAndTooltip(
        "Verify Checksum",
        "Compute the J1850 CRC-8 (poly 0x1D, init 0xFF, xor-out 0xFF) and flag mismatches." );
    mVerifyChecksumInterface.SetCheckBoxText( "Verify J1850 CRC-8 checksum" );
    mVerifyChecksumInterface.SetValue( mVerifyChecksum );

    AddInterface( &mInputChannelInterface );
    AddInterface( &mActiveLevelInterface );
    AddInterface( &mVerifyChecksumInterface );

    AddExportOption( 0, "Export as CSV file" );
    AddExportExtension( 0, "csv", "csv" );
    AddExportExtension( 0, "text", "txt" );

    ClearChannels();
    AddChannel( mInputChannel, "J1850 VPW", false );
}

J1850VpwAnalyzerSettings::~J1850VpwAnalyzerSettings() {}

bool J1850VpwAnalyzerSettings::SetSettingsFromInterfaces()
{
    mInputChannel = mInputChannelInterface.GetChannel();
    mActiveLevel = static_cast<U32>( mActiveLevelInterface.GetNumber() );
    mVerifyChecksum = mVerifyChecksumInterface.GetValue();

    ClearChannels();
    AddChannel( mInputChannel, "J1850 VPW", true );
    return true;
}

void J1850VpwAnalyzerSettings::UpdateInterfacesFromSettings()
{
    mInputChannelInterface.SetChannel( mInputChannel );
    mActiveLevelInterface.SetNumber( static_cast<double>( mActiveLevel ) );
    mVerifyChecksumInterface.SetValue( mVerifyChecksum );
}

void J1850VpwAnalyzerSettings::LoadSettings( const char* settings )
{
    SimpleArchive text_archive;
    text_archive.SetString( settings );

    text_archive >> mInputChannel;
    text_archive >> mActiveLevel;
    text_archive >> mVerifyChecksum;

    ClearChannels();
    AddChannel( mInputChannel, "J1850 VPW", true );

    UpdateInterfacesFromSettings();
}

const char* J1850VpwAnalyzerSettings::SaveSettings()
{
    SimpleArchive text_archive;
    text_archive << mInputChannel;
    text_archive << mActiveLevel;
    text_archive << mVerifyChecksum;
    return SetReturnString( text_archive.GetString() );
}
