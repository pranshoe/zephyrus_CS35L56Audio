#ifndef _CS35L56_AUDIO_H
#define _CS35L56_AUDIO_H

#include "IntelLpssSpi.h"
#include <IOKit/IOService.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <IOKit/pci/IOPCIDevice.h>

#define CS35L56_DSP_VIRTUAL1_MBOX_1			    0x0011020

class CS35L56Audio : public IOService{
    OSDeclareDefaultStructors(CS35L56Audio)

    public:
        bool init(OSDictionary *dictionary = nullptr) override;
        IOService *probe(IOService *provider, SInt32 *score) override;
        bool start(IOService *provider) override;
        void stop(IOService *provider) override;
        void free() override;

    private:
        IntelLpssSpi spiController;
        bool reset();
        bool waitForFirmwareBoot(UInt8 cs);
        bool mboxSend(UInt8 cs, UInt32 comamnd);
};

#endif // _CS35L56_AUDIO_H
