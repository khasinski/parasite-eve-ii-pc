/* Widescreen culling of models entirely outside the original screen; see port/model_cull.h. */
#include "port/model_cull.h"
#include <psyz.h>
#include <libgpu.h>
#include <limits.h>
#include <stdlib.h>

enum { SCREEN_WIDTH = 320, LOG_START = 1024 };

typedef struct {
    OT_TYPE* head;
    P_TAG*   packet;
} Link;

static Link* links;
static int   count, capacity, active, keep, minX, maxX, wide;

void peModelCullSetWide(int enabled)
{
    wide = enabled != 0;
}

void peModelCullBegin(void)
{
    active = wide;
    count = keep = 0;
    minX = INT_MAX;
    maxX = INT_MIN;
}

/* Screen x of each corner of a TMD polygon packet; 0 for other packets. */
static int corners(const P_TAG* packet, short* xs)
{
    u_char code = getcode(packet);
    if ((code & 0xE0) != 0x20) return 0; /* not a polygon */
    int quad = (code & 0x08) != 0, gouraud = (code & 0x10) != 0, textured = (code & 0x04) != 0;
    if (!gouraud && !textured) {
        const POLY_F4* p = (const POLY_F4*)packet;
        xs[0] = p->x0; xs[1] = p->x1; xs[2] = p->x2; if (quad) xs[3] = p->x3;
    } else if (gouraud && !textured) {
        const POLY_G4* p = (const POLY_G4*)packet;
        xs[0] = p->x0; xs[1] = p->x1; xs[2] = p->x2; if (quad) xs[3] = p->x3;
    } else if (!gouraud) {
        const POLY_FT4* p = (const POLY_FT4*)packet;
        xs[0] = p->x0; xs[1] = p->x1; xs[2] = p->x2; if (quad) xs[3] = p->x3;
    } else {
        const POLY_GT4* p = (const POLY_GT4*)packet;
        xs[0] = p->x0; xs[1] = p->x1; xs[2] = p->x2; if (quad) xs[3] = p->x3;
    }
    return quad ? 4 : 3;
}

void peModelCullLinked(void* head, void* packet)
{
    if (!active || keep) return;
    short xs[4];
    int   n = corners(packet, xs);
    if (!n) {
        keep = 1; /* an unknown packet: never cull this model */
        return;
    }
    for (int i = 0; i < n; i++) {
        if (xs[i] < minX) minX = xs[i];
        if (xs[i] > maxX) maxX = xs[i];
    }
    if (minX < SCREEN_WIDTH && maxX >= 0) {
        keep = 1; /* reaches into the original screen */
        return;
    }
    if (count == capacity) {
        int   grown = capacity ? capacity * 2 : LOG_START;
        Link* more  = realloc(links, (size_t)grown * sizeof(*links));
        if (!more) {
            keep = 1;
            return;
        }
        links    = more;
        capacity = grown;
    }
    links[count].head   = head;
    links[count].packet = packet;
    count++;
}

void peModelCullEnd(void)
{
    if (!active || keep || !count) {
        active = 0;
        return;
    }
    /* addPrim put each packet at the front of its bucket; undoing them newest
     * first restores every bucket exactly. */
    for (int i = count - 1; i >= 0; i--) {
        OT_TYPE* head = links[i].head;
        setaddr(head, links[i].packet->addr);
    }
    active = 0;
}
