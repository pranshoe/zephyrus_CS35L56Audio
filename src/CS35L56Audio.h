#ifndef _CS35L56_AUDIO_H
#define _CS35L56_AUDIO_H

#include "IntelLpssSpi.h"
#include <IOKit/IOService.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <IOKit/pci/IOPCIDevice.h>

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
};

#endif // _CS35L56_AUDIO_H
