#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>

#define WMFW_HALO_PM_PACKED         0x10
#define WMFW_HALO_XM_PACKED         0x11
#define WMFW_HALO_YM_PACKED         0x12

#define WMFW_ADSP2_XM               5
#define WMFW_ADSP2_YM               6

#define WMFW_ABSOLUTE               0xF0
#define __packed __attribute__((packed))

struct wmfw_halo_alg_hdr {
	UInt32 id;
	UInt32 ver;
	UInt32 xm_base;
	UInt32 xm_size;
	UInt32 ym_base;
	UInt32 ym_size;
} __packed;

static inline UInt32 cpu_to_be32(UInt32 v){
    return OSSwapHostToBigInt32(v);
}

static inline UInt32 be32_to_cpu(UInt32 v){
    return OSSwapBigToHostInt32(v);
}
