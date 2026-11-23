/* Linked before every overlay object: marks the start of their writable data.
 * Initialized data, zero-initialized statics and zero-initialized external
 * definitions each have a marker of their own kind: compilers place those in
 * different sections (Mach-O keeps the external ones in __common). */
#include "port/overlay_data.h"

__attribute__((used, aligned(16))) char peOverlayDataBegin[1] = { 1 };
__attribute__((used, aligned(16))) static char peOverlayBssBeginMark[1];
char* const peOverlayBssBegin = peOverlayBssBeginMark;
__attribute__((used, nocommon, aligned(16))) char peOverlayZeroBegin[1] = { 0 };
