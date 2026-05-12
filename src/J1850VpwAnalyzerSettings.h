#ifndef J1850VPW_ANALYZER_SETTINGS_H
#define J1850VPW_ANALYZER_SETTINGS_H

#include <AnalyzerSettings.h>
#include <AnalyzerTypes.h>

class J1850VpwAnalyzerSettings : public AnalyzerSettings
{
  public:
    J1850VpwAnalyzerSettings();
    virtual ~J1850VpwAnalyzerSettings();

    virtual bool SetSettingsFromInterfaces();
    void UpdateInterfacesFromSettings();
    virtual void LoadSettings( const char* settings );
    virtual const char* SaveSettings();

    Channel mInputChannel;
    U32 mActiveLevel;       // 0 = active low (typical transceiver output), 1 = active high
    bool mVerifyChecksum;   // run J1850 CRC-8 verification

  protected:
    AnalyzerSettingInterfaceChannel    mInputChannelInterface;
    AnalyzerSettingInterfaceNumberList mActiveLevelInterface;
    AnalyzerSettingInterfaceBool       mVerifyChecksumInterface;
};

#endif
