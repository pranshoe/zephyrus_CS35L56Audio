#include "IntelLpssSpi.h"
#include <IOKit/IOService.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <IOKit/pci/IOPCIDevice.h>

bool IntelLpssSpi::init(){
    IORegistryEntry *entry = IORegistryEntry::fromPath("IOService:/AppleACPIPlatformExpert/PC00@0/AppleACPIPCI/SPI0@1E,2");

    if(!entry){
        IOLog("IntelLpssSpi: Entry of SPI Controller doesn't exist\n");
        return false;
    }

    spiDev = OSDynamicCast(IOPCIDevice, entry);
    if(!spiDev){
        IOLog("IntelLpssSpi: SPIDevice is not an IOPCI Device");
        entry->release();
        return false;
    }

    UInt8 pmscr = 0;
    if(spiDev->findPCICapability(kIOPCIPowerManagementCapability, &pmscr) == 0){
        IOLog("IntelLpssSpi: Cannot get the location of pmscr for SPI Controller\n");
        return false;
    }

    UInt16 pmscr_val = spiDev->configRead16(pmscr + 0x4);
    if(pmscr_val & 0x0003){
        IOLog("IntelLpssSpi: SPI Controller is sleeping, waking up...\n");
        spiDev->configWrite16(pmscr + 0x4, pmscr_val & ~0x0003);
        IODelay(10000);
    }

    spiDev->setMemoryEnable(true);
    spiDev->setBusMasterEnable(true);

    spiMemoryMap = spiDev->mapDeviceMemoryWithIndex(0);
    if(!spiMemoryMap){
        IOLog("IntelLpssSpi: Failed to map BAR0 for Spi Device\n");
        spiDev->release();
        spiDev=nullptr;
        return false;
    }

    spiBase = (volatile UInt8 *)spiMemoryMap->getVirtualAddress();
    IOLog("IntelLpssSpi: Successfully mapped SPI BAR0 to virtual memory address %p\n", spiBase);

    UInt32 sscr0 = *(volatile UInt32 *)spiBase;
    IOLog("IntelLpssSpi: Current value of SSCR0 = 0x%08x\n", sscr0);

    return true;
}

void IntelLpssSpi::free(){
    if(map130) {
        map130->release();
        map130 = nullptr;
    }
    if(spiMemoryMap){
        spiMemoryMap->release();
        spiMemoryMap=nullptr;
    } if(spiDev){
        spiDev->release();
        spiDev=nullptr;
    }
    spiBase=nullptr;
}

bool IntelLpssSpi::configure(){
    volatile UInt32 *clock = (volatile UInt32 *)(spiBase + 0x200);
    UInt32 clock_val = *clock;
    clock_val |= (1<<0);
    *clock = clock_val;

    volatile UInt32 *reset = (volatile UInt32 *)(spiBase + 0x204);
    UInt32 reset_val = *reset;
    reset_val |= (7 << 0);
    *reset = reset_val;
    IODelay(100);

    volatile UInt32 *control = (volatile UInt32 *)(spiBase + 0x224);
    UInt32 control_val = *control;
    control_val |= (1<<1)|(1<<0);
    *control = control_val;

    volatile UInt32 *sscr0 = (volatile UInt32 *)spiBase;
    *sscr0 = 0;
    volatile UInt32 *sscr1 = (volatile UInt32 *)(spiBase + 0x04);
    *sscr1 = 0;

    volatile UInt32 *sssr = (volatile UInt32 *)(spiBase + 0x08);
    *sssr = 0xFFFFFFFF;
    volatile UInt32 *ssdr = (volatile UInt32 *)(spiBase + 0x10);
    UInt32 nice, limit=64;
    while(( *sssr & (1<<3) ) && --limit!=0) nice = *ssdr;

    UInt32 sscr0_val = *sscr0;
    sscr0_val |= (12<<8) | (1<<7) | (7<<0);
    *sscr0 = sscr0_val;


    //Pin 130 for CS1
    IORegistryEntry *gpi0Entry = IORegistryEntry::fromPath("IOService:/AppleACPIPlatformExpert/GPI0");
    if (!gpi0Entry) return false;

    IOACPIPlatformDevice *gpi0Dev = OSDynamicCast(IOACPIPlatformDevice, gpi0Entry);
    if (!gpi0Dev) { gpi0Entry->release(); return false; }

    map130 = gpi0Dev->mapDeviceMemoryWithIndex(1);
    gpi0Entry->release();
    if (!map130) return false;

    volatile UInt8 *communityBase = (volatile UInt8 *)map130->getVirtualAddress();
    UInt32 padbar = *(volatile UInt32 *)(communityBase + 0x000C);
    UInt32 padIndex = 27;
    padCfg130 = (volatile UInt32 *)(communityBase + padbar + (padIndex * 16));

    *padCfg130 &= ~(7<<10);
    *padCfg130 &= ~(1<<8);
    *padCfg130 |= (1<<0);

    return true;
}

bool IntelLpssSpi::setCS(UInt8 cs, bool active){
    volatile UInt32 *control = (volatile UInt32 *)(spiBase + 0x224);
    UInt32 control_val = *control;

    if(cs==0){
        control_val |= (1<<0);
        control_val &= ~(3<<8);
        control_val &= ~(3 << 8);

        if(active){
            control_val &= ~(1<<1);
        } else {
            control_val |= (1<<1);
        }
        *control = control_val;
        IODelay(10);
        IOLog("IntelLpssSpi: setCS(cs=%u, active=%d) -> 0x224 is now 0x%08x\n", cs, active, *control);
        return true;
    }

    if(cs == 1){
        if(!padCfg130) return false;
        if(active) *padCfg130 &= ~(1<<0);
        else *padCfg130 |= (1<<0);
        IODelay(10);
        return true;
    }
    return false;
}

void IntelLpssSpi::transfer(UInt8 cs, UInt8 *tx, UInt8 *rx, UInt32 length){
    setCS(cs, true);

    volatile UInt32 *sssr = (volatile UInt32 *)(spiBase + 0x8);
    volatile UInt32 *ssdr = (volatile UInt32 *)(spiBase + 0x10);
    SInt32 timeout;
    for(SInt32 i=0; i<length; i++){
        timeout = 5000;
        while(!(*sssr & (1<<2)) && --timeout>0){
            IODelay(1);
        }
        if(timeout==0){
            IOLog("IntelLpssSpi: Tx FIFO timeout\n");
            break;
        }
        *ssdr = tx[i];

        timeout = 5000;
        while(!(*sssr & (1<<3)) && --timeout > 0){
            IODelay(1);
        }
        if(timeout == 0){
            IOLog("IntelLpssSpi: Rx FIFO timeout\n");
            break;
        }
        UInt32 data = *(ssdr);
        if(rx){
            rx[i] = (data&0xFF);
        }
    }

    timeout = 5000;
    while((*sssr & (1<<4)) && --timeout > 0){
        IODelay(1);
    }
    setCS(cs, false);
}


UInt32 IntelLpssSpi::readReg32(UInt8 cs, UInt32 addr){
    UInt8 tx[10]={
        (UInt8)((1<<7) | ((addr & 0x7F000000) >> 24)), (UInt8)((addr & 0x00FF0000) >> 16), (UInt8)((addr & 0x0000FF00) >> 8), (UInt8)(addr & 0x000000FF),
        0, 0,
        0, 0, 0, 0};

    UInt8 rx[10]={0};

    transfer(cs, tx, rx, 10);
    return (rx[6]<<24) | (rx[7] << 16) | (rx[8] << 8) | rx[9];
}

void IntelLpssSpi::writeReg32(UInt8 cs, UInt32 addr, UInt32 value){
    UInt8 tx[8] = {
        (UInt8)((addr & 0x7F000000) >> 24), (UInt8)((addr & 0x00FF0000) >> 16), (UInt8)((addr & 0x0000FF00) >> 8), (UInt8)(addr & 0x000000FF),
        (UInt8)((value & 0xFF000000) >> 24), (UInt8)((value & 0x00FF0000) >> 16), (UInt8)((value & 0x0000FF00) >> 8), (UInt8)(value & 0x000000FF)
    };

    transfer(cs, tx, nullptr, 8);
}
