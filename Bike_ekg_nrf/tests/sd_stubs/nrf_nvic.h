#pragma once
// Single-Thread-Hosttests: kritische Bereiche sind keine echte Interruptsperre.
#include <stdint.h>
inline int sd_nvic_critical_region_enter(uint8_t*) { return 0; }
inline int sd_nvic_critical_region_exit(uint8_t) { return 0; }
