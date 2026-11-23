#ifndef PORT_OVERLAY_DATA_H
#define PORT_OVERLAY_DATA_H

#include <stddef.h>

/* The PS1 loads stage, room and actor overlays from the disc, so every return
 * to the title starts them from their initial data. Native overlays stay
 * resident: their writable data is linked between the markers below
 * (tools/port/check_overlay_data_span.py checks it), recorded at the first
 * game initialization and restored at each later one. Their -fcommon
 * tentative definitions get real ones among it (gen_overlay_commons.py). */
extern char peOverlayDataBegin[], peOverlayDataEnd[];
extern char* const peOverlayBssBegin;
extern char* const peOverlayBssEnd;
extern char peOverlayZeroBegin[], peOverlayZeroEnd[];

/* Record the initial overlay data; later calls do nothing. */
void peOverlayDataSnapshot(void);
/* Return overlay data to the recorded state; nothing before the snapshot. */
void peOverlayDataRestore(void);
/* Make bytes filled after the snapshot (disc 2 assets) part of it. */
void peOverlayDataRefresh(const void* address, size_t bytes);
/* 1 when address lies in the restored overlay data. */
int peOverlayDataContains(const void* address);

#endif
