/* Linked after every overlay object: marks the end of their writable data. */
#include "port/overlay_data.h"

__attribute__((used)) char peOverlayDataEnd[1] = { 1 };
__attribute__((used)) static char peOverlayBssEndMark[1];
char* const peOverlayBssEnd = peOverlayBssEndMark;
__attribute__((used, nocommon)) char peOverlayZeroEnd[1] = { 0 };
