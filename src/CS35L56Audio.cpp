#include "CS35L56Audio.h"
#include "IntelLpssSpi.h"
#include "CS35L56_Rev3.11.16_wmfw.h"
#include "cs35l56_10431c63_spkid1_amp1_bin.h"
#include "cs35l56_10431c63_spkid1_amp2_bin.h"
#include "wmfw.h"
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

    if(!loadFw()){
        IOLog("CS35L56Audio: Failed to load firmware\n");
        return false;
    }

    if(!configure()){
        IOLog("CS35L56Audio: Configuring Amps failed\n");
        return false;
    }

    play();

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

    if(!waitForFirmwareBoot(0) || !waitForFirmwareBoot(1)){
        return false;
    }
    return true;
}

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

bool CS35L56Audio::configure(){
    for(SInt32 i=0; i<2; i++){
        IOLog("CS35L56Audio: Reset for CS%d\n",i);
        if(!mboxSend(i, CS35L56_MBOX_CMD_PREVENT_AUTO_HIBERNATE))
            return false;
        spiController.updateReg32(i, CS35L56_ASP1_CONTROL3, CS35L56_ASP1_DOUT_HIZ_CTRL_MASK, CS35L56_ASP1_DOUT_HIZ_CTRL_MASK);

        spiController.updateReg32(i, CS35L56_IRQ1_MASK_1, CS35L56_AMP_SHORT_ERR_EINT1_MASK, 0);
        spiController.updateReg32(i, CS35L56_IRQ1_MASK_8, CS35L56_TEMP_ERR_EINT1_MASK, 0);


        /*
          * This reg sequence configures the device as
          *  ASP1_BCLK_FREQ = 3.072 MHz
          *  ASP1_RX_WIDTH = 32 cycles per slot, ASP1_TX_WIDTH = 32 cycles per slot, ASP1_FMT = I2S
          *  ASP1_DOUT_HIZ_CONTROL = Hi-Z during unused timeslots
          *  ASP1_RX_WL = 24 bits per sample
          *  ASP1_TX_WL = 24 bits per sample
          *  ASP1_RXn_EN 1..3 and ASP1_TXn_EN 1..4 disabled
          *
          * Override any Windows-specific mixer settings applied by the firmware.
          */
        spiController.writeReg32(i, CS35L56_ASP1_CONTROL1, 0x00000021);
        spiController.writeReg32(i, CS35L56_ASP1_CONTROL2, 0x20200200);
        spiController.writeReg32(i, CS35L56_ASP1_CONTROL3, 0x00000003);
        spiController.writeReg32(i, CS35L56_ASP1_FRAME_CONTROL1, 0x03020100);
        spiController.writeReg32(i, CS35L56_ASP1_FRAME_CONTROL5, 0x00020100);
        spiController.writeReg32(i, CS35L56_ASP1_DATA_CONTROL1, 0x00000018);
        spiController.writeReg32(i, CS35L56_ASP1_DATA_CONTROL5, 0x00000018);
        spiController.writeReg32(i, CS35L56_ASP1_ENABLES1, 0x00000000);
        spiController.writeReg32(i, CS35L56_ASP1TX1_INPUT, 0x00000018);
        spiController.writeReg32(i, CS35L56_ASP1TX2_INPUT, 0x00000019);
        spiController.writeReg32(i, CS35L56_ASP1TX3_INPUT, 0x00000020);
        spiController.writeReg32(i, CS35L56_ASP1TX4_INPUT, 0x00000028);
    }
    return true;
}

bool CS35L56Audio::loadFw(){
    for(SInt32 i=0; i<2; i++){
        mboxSend(i, CS35L56_MBOX_CMD_SHUTDOWN);

        SInt32 timeout = 250;
        UInt32 val = 0;
        while(--timeout>0){
            val = spiController.readReg32(i, CS35L56_DSP1_PM_CUR_STATE);
            if(val == CS35L56_HALO_STATE_SHUTDOWN) break;
            IOSleep(1);
        }

        if(val != CS35L56_HALO_STATE_SHUTDOWN){
            IOLog("CS35L56Audio: Cannot shutdown CS%d\n", i);
            return false;
        }

        //Load the WMFW
        UInt32 pos = 40;
        while(pos < firmware_CS35L56_Rev3_11_16_wmfw_len){
            UInt32 word = *(UInt32 *)&firmware_CS35L56_Rev3_11_16_wmfw[pos];
            UInt32 len = *(UInt32 *)&firmware_CS35L56_Rev3_11_16_wmfw[pos+4];
            UInt32 off = word & 0xFFFFFF;
            UInt32 type = word >> 24;

            UInt32 reg = 0;
            switch(type){
                case WMFW_HALO_XM_PACKED: reg = (0x02000000 + off*3) & ~3u; break;
                case WMFW_HALO_YM_PACKED: reg = (0x02C00000 + off*3) & ~3u; break;
                case WMFW_HALO_PM_PACKED: reg = 0x03800000 + off*5; break;
                case WMFW_ADSP2_XM: reg = 0x02800000 + off*4; break;
                case WMFW_ADSP2_YM: reg = 0x03400000 + off*4; break;
                case WMFW_ABSOLUTE: reg = off;
            }

            if(reg) spiController.burstWrite(i, reg, firmware_CS35L56_Rev3_11_16_wmfw+pos+8, len);
            pos += 8 +len;
        }


        //setupAlgs
        UInt32 n_algs;

        n_algs = spiController.readReg32(i, CS35L56_DSP1_XMEM_UNPACKED24_0 + 0x24); //Condensed from the source.
        if(n_algs == 0 || n_algs > 64) {
            IOLog("CS35L56Audio: CS%d: No algs found\n", i);
            return false;
        }

        UInt32 term = spiController.readReg32(i, CS35L56_DSP1_XMEM_UNPACKED24_0 + 0x28 + n_algs*24);
        if(term != 0x00bedead){
            IOLog("CS35L56Audio: CS%d: Terminator for algorithms not found\n");
            return false;
        }

        struct wmfw_halo_alg_hdr haloAlgs[64];
        haloAlgs[0].id = 0x001a00d6;
        haloAlgs[0].xm_base = 0;
        haloAlgs[0].ym_base = 0;
        spiController.burstRead(i, CS35L56_DSP1_XMEM_UNPACKED24_0  + 0x28, &haloAlgs[1], n_algs * sizeof(wmfw_halo_alg_hdr));
        for(UInt32 k = 1; k<=n_algs; k++){
            haloAlgs[k].id = be32_to_cpu(haloAlgs[k].id);
            haloAlgs[k].ver = be32_to_cpu(haloAlgs[k].ver);
            haloAlgs[k].xm_base = be32_to_cpu(haloAlgs[k].xm_base);
            haloAlgs[k].xm_size = be32_to_cpu(haloAlgs[k].xm_size);
            haloAlgs[k].ym_base = be32_to_cpu(haloAlgs[k].ym_base);
            haloAlgs[k].ym_size = be32_to_cpu(haloAlgs[k].ym_size);
        }


        //Load Coefficients
        UInt8 *file = (i==0 ? firmware_cs35l56_10431c63_spkid1_amp1_bin : firmware_cs35l56_10431c63_spkid1_amp2_bin);
        UInt32 file_size = (i==0 ? firmware_cs35l56_10431c63_spkid1_amp1_bin_len : firmware_cs35l56_10431c63_spkid1_amp2_bin_len);
        pos = *(UInt32 *)&file[4];
        while(pos < file_size){
            UInt16 offset = *(UInt16 *)&file[pos];
            UInt16 type = *(UInt16 *)&file[pos+2];
            UInt32 blk_id = *(UInt32 *)&file[pos+4];
            UInt32 len = *(UInt32 *)&file[pos+16];
            UInt8 *data = (UInt8 *)&file[pos+20];

            UInt32 reg = 0;

            SInt32 alg_idx = -1;
            for(UInt32 k=0; k<=n_algs; k++){
                if(blk_id == haloAlgs[k].id){
                    alg_idx = k;
                    break;
                }
            }

            if(alg_idx!=-1){
                switch(type){
                    case WMFW_HALO_XM_PACKED: reg = ((0x02000000 + haloAlgs[alg_idx].xm_base*3) & ~3u) + offset; break;
                    case WMFW_HALO_YM_PACKED: reg = ((0x02C00000 + haloAlgs[alg_idx].ym_base*3) & ~3u) + offset; break;
                    case WMFW_ADSP2_XM: reg = 0x02800000 + haloAlgs[alg_idx].xm_base*4 + offset; break;
                    case WMFW_ADSP2_YM: reg = 0x03400000 + haloAlgs[alg_idx].ym_base*4 + offset; break;
                }
            }


            if(reg){
                spiController.burstWrite(i, reg, data, len);
            }

            pos+=(len + 20 + 3) & ~3u;
        }

        spiController.writeReg32(i, CS35L56_DSP1_HALO_STATE, 0);
        if(!mboxSend(i, CS35L56_MBOX_CMD_SYSTEM_RESET)){
            IOLog("CS35L56Audio: CS%d: Failed to send system reset command\n", i);
            return false;
        }

        if(!waitForFirmwareBoot(i)){
            IOLog("CS35L56Audio: CS%d: Failed to boot new firmware\n", i);
            return false;
        }

    }
    return true;
}

void CS35L56Audio::setVolume(SInt32 volume){
    if(volume < 0 || volume > (CS35L56_MAIN_RENDER_USER_VOLUME_MAX - CS35L56_MAIN_RENDER_USER_VOLUME_MIN)){
        return;
    }

    UInt32 raw_volume = (volume + CS35L56_MAIN_RENDER_USER_VOLUME_MIN) << CS35L56_MAIN_RENDER_USER_VOLUME_SHIFT;
    spiController.updateReg32(0, CS35L56_MAIN_RENDER_USER_VOLUME, CS35L56_MAIN_RENDER_USER_VOLUME_MASK, raw_volume);
    spiController.updateReg32(1, CS35L56_MAIN_RENDER_USER_VOLUME, CS35L56_MAIN_RENDER_USER_VOLUME_MASK, raw_volume);
    return;
}

void CS35L56Audio::setMute(bool mute){
    spiController.updateReg32(0, CS35L56_MAIN_RENDER_USER_MUTE, CS35L56_MAIN_RENDER_USER_MUTE_MASK, mute);
    spiController.updateReg32(1, CS35L56_MAIN_RENDER_USER_MUTE, CS35L56_MAIN_RENDER_USER_MUTE_MASK, mute);
}

void CS35L56Audio::play(){
    for(SInt32 i=0; i<2; i++){
        mboxSend(i, CS35L56_MBOX_CMD_AUDIO_PLAY);

        SInt32 timeout = 100;
        UInt32 val = 0;
        while(--timeout > 0){
            val = spiController.readReg32(i, CS35L56_TRANSDUCER_ACTUAL_PS);
            if(val == 0) break;
            IOSleep(1);
        }

        spiController.writeReg32(i, CS35L56_ASP1_ENABLES1, (1<<CS35L56_ASP_RX1_EN_SHIFT) | (1<<CS35L56_ASP_RX2_EN_SHIFT) | (1<<i));
    }
}
