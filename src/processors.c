/* processors.c — see processors.h. */
#include "processors.h"
#include <string.h>

const dxm_processor dxm_processors[DXM_NPROCESSORS] = {
    {"486dx-33", "486DX 33 MHz", "486DX", 33, 10000},
    {"486dx-40", "486DX 40 MHz", "486DX", 40, 14000},
    {"486dx2-50", "486DX2 50 MHz", "486DX2", 50, 20000},
    {"486dx2-66", "486DX2 66 MHz", "486DX2", 66, 30000},
    {"486dx4-75", "486DX4 75 MHz", "486DX4", 75, 40000},
    {"pentium-100", "Pentium-S 100 MHz", "Pentium-S", 100, 60000},
    {"pentium-133", "Pentium-S 133 MHz", "Pentium-S", 133, 100000},
    {"pentium-166", "Pentium-S 166 MHz", "Pentium-S", 166, 125000},
    {"pentium-mmx-200", "Pentium-MMX 200 MHz", "Pentium-MMX", 200, 150000},
    {"pentium-mmx-233", "Pentium-MMX 233 MHz", "Pentium-MMX", 233, 175000},
};

int dxm_processor_find(const char *code) {
    for (int i = 0; i < DXM_NPROCESSORS; i++)
        if (!strcmp(dxm_processors[i].code, code))
            return i;
    return -1;
}
