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
#define CS35L56_ASP1_ENABLES1				    0x0004800
#define CS35L56_ASP1_CONTROL1                   0x0004804
#define CS35L56_ASP1_CONTROL2				    0x0004808
#define CS35L56_ASP1_CONTROL3				    0x000480C
#define CS35L56_ASP1_FRAME_CONTROL1			    0x0004810
#define CS35L56_ASP1_FRAME_CONTROL5			    0x0004820
#define CS35L56_ASP1_DATA_CONTROL1			    0x0004830
#define CS35L56_ASP1_DATA_CONTROL5			    0x0004840
#define CS35L56_ASP1TX1_INPUT				    0x0004C20
#define CS35L56_ASP1TX2_INPUT				    0x0004C24
#define CS35L56_ASP1TX3_INPUT				    0x0004C28
#define CS35L56_ASP1TX4_INPUT				    0x0004C2C
#define CS35L56_DSP1_PM_CUR_STATE			    0x2804308

// IRQ_MASK_1
#define CS35L56_AMP_SHORT_ERR_EINT1_MASK	    0x80000000

//IRQ_MASK_8
#define CS35L56_TEMP_ERR_EINT1_MASK             0x80000000

// ASP_CONTROL3
#define CS35L56_ASP1_DOUT_HIZ_CTRL_MASK			0x00000003

#define CS35L56_HALO_STATE_SHUTDOWN			    1

#define CS35L56_MBOX_CMD_PREVENT_AUTO_HIBERNATE	0x02000003
#define CS35L56_MBOX_CMD_SHUTDOWN			    0x02000005


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
        bool configure();
        bool loadFw();
};

#endif // _CS35L56_AUDIO_H
