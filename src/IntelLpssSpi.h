#ifndef _INTEL_LPSS_SPI_H
#define _INTEL_LPSS_SPI_H
#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>

class IntelLpssSpi{
    public:
        bool init();
        void free();

        bool configure();
        bool setCS(UInt8 cs, bool active);
        void transfer(UInt8 cs, UInt8 *tx, UInt8 *rx, UInt32 length);
        UInt32 readReg32(UInt8 cs, UInt32 addr);
        void writeReg32(UInt8 cs, UInt32 addr, UInt32 value);
        void updateReg32(UInt8 cs, UInt32 addr, UInt32 mask, UInt32 value);

    private:
        IOPCIDevice *spiDev{nullptr};
        IOMemoryMap *spiMemoryMap{nullptr};
        volatile UInt8 *spiBase{nullptr};

        volatile UInt32 *padCfg130{nullptr};
        IOMemoryMap *map130{nullptr};
};

#endif
