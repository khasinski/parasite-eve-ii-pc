/* Snapshot and restore of the overlays' writable data; see port/overlay_data.h. */
#include "port/overlay_data.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    char *begin, *end;
    unsigned char* saved;
} Span;

static Span spans[3];
static int  recorded, disabled;

static size_t spanBytes(const Span* s)
{
    return s->end > s->begin ? (size_t)(s->end - s->begin) : 0;
}

/* Byte copies: sanitizers pad globals with poisoned red zones, which the
 * snapshot copies unchanged. volatile keeps the loop from becoming memcpy. */
#if defined(__clang__) || defined(__GNUC__)
__attribute__((no_sanitize("address")))
#endif
static void copyBytes(void* to, const void* from, size_t bytes)
{
    volatile unsigned char* d = to;
    const volatile unsigned char* s = from;
    for (size_t i = 0; i < bytes; i++) d[i] = s[i];
}

void peOverlayDataSnapshot(void)
{
    if (recorded || disabled) return;
    /* On ELF and COFF the zero-initialized external span lies inside the bss one. */
    Span all[3] = { { peOverlayDataBegin, peOverlayDataEnd, NULL },
                    { peOverlayBssBegin, peOverlayBssEnd, NULL },
                    { peOverlayZeroBegin, peOverlayZeroEnd, NULL } };
    for (int i = 0; i < 3; i++) {
        size_t bytes = spanBytes(&all[i]);
        /* The markers are only meaningful when the linker kept the overlay
         * objects between them; an empty or implausible span disables restoring. */
        if (!bytes || bytes > (256u << 20)) {
            fprintf(stderr, "[overlays] data markers out of order (span %d: %zu bytes); state is not reset on restart\n", i, bytes);
            disabled = 1;
        } else if (!(all[i].saved = malloc(bytes))) {
            fprintf(stderr, "[overlays] cannot keep initial data; state is not reset on restart\n");
            disabled = 1;
        }
    }
    if (disabled) {
        for (int i = 0; i < 3; i++) free(all[i].saved);
        return;
    }
    for (int i = 0; i < 3; i++) {
        copyBytes(all[i].saved, all[i].begin, spanBytes(&all[i]));
        spans[i] = all[i];
    }
    recorded = 1;
}

void peOverlayDataRestore(void)
{
    if (!recorded) return;
    for (int i = 0; i < 3; i++) copyBytes(spans[i].begin, spans[i].saved, spanBytes(&spans[i]));
}

void peOverlayDataRefresh(const void* address, size_t bytes)
{
    if (!recorded) return;
    const char *from = address, *to = from + bytes;
    for (int i = 0; i < 3; i++) {
        const char *a = from < spans[i].begin ? spans[i].begin : from, *b = to > spans[i].end ? spans[i].end : to;
        if (a < b) copyBytes(spans[i].saved + (a - spans[i].begin), a, (size_t)(b - a));
    }
}

int peOverlayDataContains(const void* address)
{
    const char* p = address;
    return (p >= peOverlayDataBegin && p < peOverlayDataEnd) || (p >= peOverlayBssBegin && p < peOverlayBssEnd) ||
           (p >= peOverlayZeroBegin && p < peOverlayZeroEnd);
}
