#include "cpu-defs.h"

typedef enum {
#ifdef TARGET_PPC64
    NIP_64 = 64,
    MSR_64 = 65,
    LR_64 = 67,
    CTR_64 = 68,
    XER_64 = 69,
#endif
#ifdef TARGET_PPC32
    NIP_32 = 64,
    MSR_32 = 65,
    LR_32 = 67,
    CTR_32 = 68,
    XER_32 = 69,
#endif
} Registers;