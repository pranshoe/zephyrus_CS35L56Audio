#ifndef _CS35L56_AUDIO_H
#define _CS35L56_AUDIO_H

#include "IntelLpssSpi.h"
#include <IOKit/IOService.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <IOKit/pci/IOPCIDevice.h>

#define CS35L56_IRQ1_MASK_1                     0x000E090
#define CS35L56_IRQ1_MASK_8				        0x000E0AC
#define CS35L56_ASP1_CONTROL3				    0x000480C
#define CS35L56_DSP_VIRTUAL1_MBOX_1			    0x0011020


// IRQ_MASK_1
#define CS35L56_AMP_SHORT_ERR_EINT1_MASK	    0x80000000

//IRQ_MASK_8
#define CS35L56_TEMP_ERR_EINT1_MASK             0x80000000

// ASP_CONTROL3
#define CS35L56_ASP1_DOUT_HIZ_CTRL_MASK			0x00000003

#define CS35L56_MBOX_CMD_PREVENT_AUTO_HIBERNATE	0x02000003

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
