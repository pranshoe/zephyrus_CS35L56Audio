#include "CS35L56Audio.h"
#include "IntelLpssSpi.h"
#include <mach/kmod.h>
#include <IOKit/IOLib.h>

extern "C" kern_return_t _start(kmod_info_t *ki, void *d);
extern "C" kern_return_t _stop(kmod_info_t *ki, void *d);

extern "C" {
    KMOD_EXPLICIT_DECL(com.zephyrus.CS35L56Audio, "1.0.0", _start, _stop)
}

extern "C" kern_return_t _start(kmod_info_t *ki, void *d) {
    return KERN_SUCCESS;
}

extern "C" kern_return_t _stop(kmod_info_t *ki, void *d) {
    return KERN_SUCCESS;
}

OSDefineMetaClassAndStructors(CS35L56Audio, IOService);

bool CS35L56Audio::init(OSDictionary *dictionary){
    if(!IOService::init(dictionary))
        return false;

    IOLog("CS35L56Audio: init() called\n");
    return true;
}


IOService *CS35L56Audio::probe(IOService *provider, SInt32 *score){
    if(!IOService::probe(provider, score)) return nullptr;
    IOLog("CS35L56Audio: probe() matched a provider: %s\n", provider->getName());

    return this;
}

bool CS35L56Audio::start(IOService* provider){
    if(!IOService::start(provider))
        return false;

    IOLog("CS35L56Audio: Hello\n");
    IOLog("CS35L56Audio: Attached to ASUS Zephyrus G16\n");

    IOACPIPlatformDevice *acpiDevice = OSDynamicCast(IOACPIPlatformDevice, provider);
    if(!acpiDevice){
        IOLog("CS35L56Audio: Provider is not an IOACPIPlatformDevice\n");
        return false;
    }

    UInt32 sta= 0;
    if(acpiDevice->evaluateInteger("_STA", &sta) == kIOReturnSuccess){
        IOLog("CS35L56Audio: ACPI _STA = 0x%x (%s)\n", sta, (sta & 0x01) ? "Device Present" : "Device Absent");
    }

    OSObject *uidObj = nullptr;
    if(acpiDevice->evaluateObject("_UID", &uidObj) == kIOReturnSuccess && uidObj){
        OSString *uidStr = OSDynamicCast(OSString, uidObj);
        OSNumber *uidNum = OSDynamicCast(OSNumber, uidObj);
        if(uidStr){
            IOLog("CS35L56Audio: ACPI _UID = %s\n", uidStr->getCStringNoCopy());
        } else if(uidNum){
            IOLog("CS35L56Audio: ACPI _UID = %u\n", uidNum->unsigned32BitValue());
        }
        uidObj->release();
    }

    IORegistryEntry *spiAcpi = provider->getParentEntry(gIOACPIPlane);
    if(spiAcpi){
        IOACPIPlatformDevice *spiAcpiDev = OSDynamicCast(IOACPIPlatformDevice, spiAcpi);
        if(spiAcpiDev){
            spiAcpiDev->evaluateObject("_PS0", nullptr);
            IOLog("CS35L56Audio: Woke up Power Lines for the parent SPI Controller\n");
            IODelay(10000);
        }
    }

    acpiDevice->evaluateObject("_PS0", nullptr);
    IOLog("CS35L56Audio: Woke up the amplifiers\n");
    IODelay(10000);

    if(!spiController.init()){
        IOLog("CS35L56Audio: Couldn't init SPI Controller\n");
        return false;
    }

    if(!spiController.configure()){
        IOLog("CS35L56Audio: Failed to configure the SPI Controller\n");
        return false;
    }

    if(!reset()){
        IOLog("CS35L56Audio: Hardware Reset Failed\n");
        return false;
    }

    for (UInt8 cs = 0; cs < 2; cs++) {
        UInt8 tx[10] = {0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        UInt8 rx[10] = {0};
        spiController.transfer(cs, tx, rx, 10);
        IOLog("CS35L56Audio: CS%u Raw RX: %02x %02x %02x %02x %02x %02x [%02x %02x %02x %02x]\n",
              cs, rx[0], rx[1], rx[2], rx[3], rx[4], rx[5], rx[6], rx[7], rx[8], rx[9]);
    }

    registerService();
    return true;
}

void CS35L56Audio::stop(IOService *provider){
    IOLog("CS35L56Audio: stop() called\n");
    spiController.free();
    IOService::stop(provider);
}

void CS35L56Audio::free(){
    IOLog("CS35L56Audio: free() called\n");
    IOService::free();
}

bool CS35L56Audio::reset(){
    IOLog("CS35L56Audio: Checking hardware reset line state...\n");

    IORegistryEntry *gpi0Entry = IORegistryEntry::fromPath("IOService:/AppleACPIPlatformExpert/GPI0");
    if (!gpi0Entry) return false;

    IOACPIPlatformDevice *gpi0Dev = OSDynamicCast(IOACPIPlatformDevice, gpi0Entry);
    if (!gpi0Dev) { gpi0Entry->release(); return false; }

    IOMemoryMap *map = gpi0Dev->mapDeviceMemoryWithIndex(4);
    gpi0Entry->release();
    if (!map) return false;

    volatile UInt8 *communityBase = (volatile UInt8 *)map->getVirtualAddress();
    UInt32 padbar = *(volatile UInt32 *)(communityBase + 0x000C);
    UInt32 padIndex = 26;
    volatile UInt32 *padCfgDw0 = (volatile UInt32 *)(communityBase + padbar + (padIndex * 16));

    UInt32 val = *padCfgDw0;

    *padCfgDw0 &= ~(7<<10);
    *padCfgDw0 &= ~((1<<8) | (1<<9));
    *padCfgDw0 &= ~(1<<0);
    IODelay(2000);

    *padCfgDw0 |= (1<<0);
    IODelay(5000);

    val = *padCfgDw0;
    map->release();
    if (!(val & (1 << 0))) {
        IOLog("CS35L56Audio: Warning: Pin 385 is LOW (0V). Amplifier is in reset!\n");
        return false;
    }
    IOLog("CS35L56Audio: Pin 385 is 3.3V (HIGH). Amplifier is awake and running.\n");


bool CS35L56Audio::waitForFirmwareBoot(UInt8 cs){
    UInt32 mbox = 0;
    UInt32 timeout = 500;
    while(--timeout > 0){
        mbox = spiController.readReg32(cs, 0x0011024);
        if(mbox > 0 && mbox < 0xFFFFFFFF){
            IOLog("CS35L56Audio: Firmware is loaded and booted\n");
            return true;
        }

        IOSleep(1);
    }

    IOLog("CS35L56Audio: Firmware isnt loaded\n");
    return false;
}

bool CS35L56Audio::mboxSend(UInt8 cs, UInt32 command){
    spiController.writeReg32(cs, CS35L56_DSP_VIRTUAL1_MBOX_1, command);
    SInt32 timeout = 100;
    UInt32 ret;
    while(--timeout > 0){
        ret = spiController.readReg32(cs, CS35L56_DSP_VIRTUAL1_MBOX_1);
        if(ret == 0){
            return true;
        } IOSleep(1);
    }

    IOLog("CS35L56Audio: CS%u mbox command 0x%08x timed out\n", cs, command);
    return false;

}
