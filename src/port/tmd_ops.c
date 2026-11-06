/*
 * Lit, untransformed-vertex draw handlers from hasm/Tmd_StreamHandlers_Ops.s.
 *
 * Each handler walks `elemCount` elements of `elemStride` words, projects the
 * element's vertices with the GTE, culls by facing, lights with the element's
 * normals and prepends the packet to its ordering-table bucket. Every element
 * consumes a packet slot, accepted or not. Packets are PsyZ POLY_* slots at
 * `ws->primWrite`; offsets in an element are byte offsets into `ws->verts` /
 * `ws->normals`. The console version reuses a quad's vertex 3 from the previous
 * element when the offsets match; that is only a cache (the result is identical
 * to projecting again), so it is not kept.
 */
#include "tmd.h"
#include "main/tmd_types.h"

#include <string.h>
#include <psyz.h>
#include <libgpu.h>
#include <libgte.h>
#include <psyz/gte.h>

enum {
    GTE_NCCS = 0x4B08041B,
    GTE_NCCT = 0x4B18043F,
    DEPTH_MASK  = 0x3FFF,
    DEPTH_SHIFT = 4,
};

static SVECTOR* vertexAt(const TmdStreamWorkspace* ws, u32 byteOffset)
{
    return (SVECTOR*)((u8*)ws->verts + byteOffset);
}

static SVECTOR* normalAt(const TmdStreamWorkspace* ws, u32 byteOffset)
{
    return (SVECTOR*)((u8*)ws->normals + byteOffset);
}

static void loadV(unsigned slot, const SVECTOR* v)
{
    Psyz_GteDataWrite(slot * 2, (u32)(u16)v->vx | ((u32)(u16)v->vy << 16));
    Psyz_GteDataWrite(slot * 2 + 1, (u32)(u16)v->vz);
}

static void storeXy(short* dst, unsigned sxyReg)
{
    /* Keep the original packed screen word, and carry its projection sidecar
     * to this exact packet address. Plain halfword stores lose that ownership
     * and the renderer deliberately does not guess by matching screen XY. */
    Psyz_GteStoreSxyTo(sxyReg, dst);
}

/* Store RGB FIFO entry `reg` (20..22) as a packet colour word: r, g, b, code. */
static void storeRgb(u8* dst, unsigned reg)
{
    u32 v = Psyz_GteDataRead(reg);
    memcpy(dst, &v, 4);
}

static void linkPacket(TmdStreamWorkspace* ws, void* packet, u32 length)
{
    u32      otz    = Psyz_GteDataRead(7);
    u32      bucket = ((otz << (ws->otDepthShift & 31)) & DEPTH_MASK) >> DEPTH_SHIFT;
    OT_TYPE* head   = (OT_TYPE*)ws->ot + bucket;

    setlen(packet, length);
    addPrim(head, packet);
}

/* Untextured Gouraud triangle, one normal per corner. */
u32* tmdDrawStreamPrimG3CornerNormals(TmdStreamWorkspace* ws, s32 flags, u32* el)
{
    u8* slot = ws->primWrite;
    (void)flags;

    for (s32 count = ws->elemCount; count != 0; count--, slot += sizeof(POLY_G3), el += ws->elemStride) {
        POLY_G3* p = (POLY_G3*)slot;
        u32 w0 = el[0], w1 = el[1], w2 = el[2], w3 = el[3];

        loadV(0, vertexAt(ws, w0 & 0xFFFF));
        loadV(1, vertexAt(ws, w0 >> 16));
        loadV(2, vertexAt(ws, w1 & 0xFFFF));
        Psyz_GteRtpt();
        if (Psyz_GteReadflg() < 0) {
            continue;
        }
        Psyz_GteNclip();
        if ((s32)Psyz_GteDataRead(24) <= 0) {
            continue;
        }
        storeXy(&p->x0, 12);
        storeXy(&p->x1, 13);
        storeXy(&p->x2, 14);
        Psyz_GteAvsz3();
        Psyz_GteDataWrite(6, w3);
        loadV(0, normalAt(ws, w1 >> 16));
        loadV(1, normalAt(ws, w2 & 0xFFFF));
        loadV(2, normalAt(ws, w2 >> 16));
        Psyz_GteCommand(GTE_NCCT);
        storeRgb(&p->r0, 20);
        storeRgb(&p->r1, 21);
        storeRgb(&p->r2, 22);
        linkPacket(ws, p, 6);
    }
    ws->primWrite = slot;
    return el;
}

/* Untextured Gouraud quad, one normal per corner. */
u32* tmdDrawStreamPrimG4CornerNormals(TmdStreamWorkspace* ws, s32 flags, u32* el)
{
    u8* slot = ws->primWrite;
    (void)flags;

    for (s32 count = ws->elemCount; count != 0; count--, slot += sizeof(POLY_G4), el += ws->elemStride) {
        POLY_G4* p = (POLY_G4*)slot;
        u32 w0 = el[0], w1 = el[1], w2 = el[2], w3 = el[3], w4 = el[4];

        /* Vertex 3 first, so its depth ends up in SZ0 after the triangle projection. */
        loadV(0, vertexAt(ws, w1 >> 16));
        Psyz_GteRtps();
        if (Psyz_GteReadflg() < 0) {
            continue;
        }
        storeXy(&p->x3, 14);

        loadV(0, vertexAt(ws, w1 & 0xFFFF));
        loadV(1, vertexAt(ws, w0 >> 16));
        loadV(2, vertexAt(ws, w0 & 0xFFFF));
        Psyz_GteRtpt(); /* SXY0..2 = vertices 2, 1, 0; SZ0..3 = vertices 3, 2, 1, 0 */
        if (Psyz_GteReadflg() < 0) {
            continue;
        }
        storeXy(&p->x2, 12);
        storeXy(&p->x1, 13);
        storeXy(&p->x0, 14);
        Psyz_GteNclip();
        if ((s32)Psyz_GteDataRead(24) >= 0) {
            /* Fallback: keep the quad if triangle (2, 1, 3) faces the viewer. */
            Psyz_GteDataWrite(14, (u32)(u16)p->x3 | ((u32)(u16)p->y3 << 16));
            Psyz_GteNclip();
            if ((s32)Psyz_GteDataRead(24) <= 0) {
                continue;
            }
        }
        Psyz_GteAvsz4();
        Psyz_GteDataWrite(6, w4);
        loadV(0, normalAt(ws, w2 & 0xFFFF));
        loadV(1, normalAt(ws, w2 >> 16));
        loadV(2, normalAt(ws, w3 & 0xFFFF));
        Psyz_GteCommand(GTE_NCCT);
        storeRgb(&p->r0, 20);
        storeRgb(&p->r1, 21);
        storeRgb(&p->r2, 22);
        loadV(0, normalAt(ws, w3 >> 16));
        Psyz_GteCommand(GTE_NCCS);
        storeRgb(&p->r3, 22);
        linkPacket(ws, p, 8);
    }
    ws->primWrite = slot;
    return el;
}

/* Untextured Gouraud triangle with one face normal: one lit colour on all corners. */
u32* tmdDrawStreamPrimG3(TmdStreamWorkspace* ws, s32 flags, u32* el)
{
    u8* slot = ws->primWrite;
    (void)flags;

    for (s32 count = ws->elemCount; count != 0; count--, slot += sizeof(POLY_G3), el += ws->elemStride) {
        POLY_G3* p = (POLY_G3*)slot;
        u32 w0 = el[0], w1 = el[1], w2 = el[2];

        loadV(0, vertexAt(ws, w0 & 0xFFFF));
        loadV(1, vertexAt(ws, w0 >> 16));
        loadV(2, vertexAt(ws, w1 & 0xFFFF));
        Psyz_GteRtpt();
        if (Psyz_GteReadflg() < 0) {
            continue;
        }
        Psyz_GteNclip();
        if ((s32)Psyz_GteDataRead(24) <= 0) {
            continue;
        }
        storeXy(&p->x0, 12);
        storeXy(&p->x1, 13);
        storeXy(&p->x2, 14);
        Psyz_GteAvsz3();
        Psyz_GteDataWrite(6, w2);
        loadV(0, normalAt(ws, w1 >> 16));
        Psyz_GteCommand(GTE_NCCS);
        storeRgb(&p->r0, 22);
        storeRgb(&p->r1, 22);
        storeRgb(&p->r2, 22);
        linkPacket(ws, p, 6);
    }
    ws->primWrite = slot;
    return el;
}

/* Untextured Gouraud quad with one face normal. */
u32* tmdDrawStreamPrimG4(TmdStreamWorkspace* ws, s32 flags, u32* el)
{
    u8* slot = ws->primWrite;
    (void)flags;

    for (s32 count = ws->elemCount; count != 0; count--, slot += sizeof(POLY_G4), el += ws->elemStride) {
        POLY_G4* p = (POLY_G4*)slot;
        u32 w0 = el[0], w1 = el[1], w2 = el[2], w3 = el[3];

        loadV(0, vertexAt(ws, w1 >> 16));
        Psyz_GteRtps();
        if (Psyz_GteReadflg() < 0) {
            continue;
        }
        storeXy(&p->x3, 14);

        loadV(0, vertexAt(ws, w1 & 0xFFFF));
        loadV(1, vertexAt(ws, w0 >> 16));
        loadV(2, vertexAt(ws, w0 & 0xFFFF));
        Psyz_GteRtpt();
        if (Psyz_GteReadflg() < 0) {
            continue;
        }
        storeXy(&p->x2, 12);
        storeXy(&p->x1, 13);
        storeXy(&p->x0, 14);
        Psyz_GteNclip();
        if ((s32)Psyz_GteDataRead(24) >= 0) {
            Psyz_GteDataWrite(14, (u32)(u16)p->x3 | ((u32)(u16)p->y3 << 16));
            Psyz_GteNclip();
            if ((s32)Psyz_GteDataRead(24) <= 0) {
                continue;
            }
        }
        Psyz_GteAvsz4();
        Psyz_GteDataWrite(6, w3);
        loadV(0, normalAt(ws, w2)); /* the whole word is the normal's byte offset */
        Psyz_GteCommand(GTE_NCCS);
        storeRgb(&p->r0, 22);
        storeRgb(&p->r1, 22);
        storeRgb(&p->r2, 22);
        storeRgb(&p->r3, 22);
        linkPacket(ws, p, 8);
    }
    ws->primWrite = slot;
    return el;
}

/*
 * Vertex pre-pass: project each element's vertex, cache its depth, light its
 * normal and scatter the screen XY and lit colour to packet words at byte offsets
 * from `ws->preXformWrite`. Elements: { vertex | normal << 16, [colour,] dest },
 * dest = XY offset | colour offset << 16.
 */
static u32* xformStreamVerts(TmdStreamWorkspace* ws, u32* el, int elementColour)
{
    u8* base = ws->preXformWrite;

    if (!elementColour) {
        Psyz_GteDataWrite(6, 0x00808080u); /* TMD_XFORM_STREAM_VERTS_COLOR */
    }
    for (s32 count = ws->elemCount; count != 0; count--, el += ws->elemStride) {
        u32 w0   = el[0];
        u32 vOff = w0 & 0xFFFF;
        u32 dest = elementColour ? el[2] : el[1];

        loadV(0, vertexAt(ws, vOff));
        Psyz_GteRtps();
        {
            u32 depth = Psyz_GteDataRead(19);
            if (Psyz_GteReadflg() < 0) {
                depth |= 0x80000000u; /* TMD_VERTEX_DEPTH_INVALID */
            }
            *(u32*)((u8*)ws->szTable + (vOff >> 1)) = depth;
        }
        if (elementColour) {
            Psyz_GteDataWrite(6, el[1]);
        }
        loadV(0, normalAt(ws, w0 >> 16));
        Psyz_GteCommand(GTE_NCCS);
        u32 rgb = Psyz_GteDataRead(22);
        /* NCCS changes colour/lighting registers, not the SXY FIFO. Register
         * this scatter at its remapped host-packet address, just like the
         * direct polygon path, while retaining the same four packet bytes. */
        Psyz_GteStoreSxyTo(14, base + (dest & 0xFFFF));
        memcpy(base + (dest >> 16), &rgb, 4);
    }
    return el;
}

u32* tmdXformStreamVertsElemColor(TmdStreamWorkspace* ws, s32 flags, u32* el)
{
    (void)flags;
    return xformStreamVerts(ws, el, 1);
}

u32* tmdXformStreamVerts(TmdStreamWorkspace* ws, s32 flags, u32* el)
{
    (void)flags;
    return xformStreamVerts(ws, el, 0);
}

/* Gouraud textured triangle, per-corner normals. UVs, CLUT and texture page are
 * already in the packet; this writes screen XY and lit colours. */
static u32* gt3(TmdStreamWorkspace* ws, u32* el, u32 colour, int reverse)
{
    u8* slot = ws->primWrite;

    for (s32 count = ws->elemCount; count != 0; count--, slot += sizeof(POLY_GT3), el += ws->elemStride) {
        POLY_GT3* p = (POLY_GT3*)slot;
        u32 w0 = el[0], w1 = el[1], w2 = el[2];

        loadV(0, vertexAt(ws, w0 & 0xFFFF));
        loadV(1, vertexAt(ws, w0 >> 16));
        loadV(2, vertexAt(ws, w1 & 0xFFFF));
        Psyz_GteDataWrite(6, colour);
        Psyz_GteRtpt();
        if (Psyz_GteReadflg() < 0) {
            continue;
        }
        Psyz_GteNclip();
        {
            s32 mac0 = (s32)Psyz_GteDataRead(24);
            if (reverse ? mac0 >= 0 : mac0 <= 0) {
                continue;
            }
        }
        storeXy(&p->x0, 12);
        storeXy(&p->x1, 13);
        storeXy(&p->x2, 14);
        Psyz_GteAvsz3();
        loadV(0, normalAt(ws, w1 >> 16));
        loadV(1, normalAt(ws, w2 & 0xFFFF));
        loadV(2, normalAt(ws, w2 >> 16));
        Psyz_GteCommand(GTE_NCCT);
        storeRgb(&p->r0, 20);
        storeRgb(&p->r1, 21);
        storeRgb(&p->r2, 22);
        linkPacket(ws, p, 9);
    }
    ws->primWrite = slot;
    return el;
}

u32* tmdDrawStreamGt3SemiTrans(TmdStreamWorkspace* ws, s32 flags, u32* el)
{
    return gt3(ws, el, 0x36808080u, (flags & TMD_OBJECT_REVERSE_CULLING) != 0);
}

u32* tmdDrawStreamGt3(TmdStreamWorkspace* ws, s32 flags, u32* el)
{
    u32 colour = (flags & TMD_OBJECT_SEMI_TRANS) ? 0x36808080u : 0x34808080u;
    return gt3(ws, el, colour, (flags & TMD_OBJECT_REVERSE_CULLING) != 0);
}

/* Gouraud textured quad, per-corner normals. */
static u32* gt4(TmdStreamWorkspace* ws, u32* el, u32 colour, int reverse)
{
    u8* slot = ws->primWrite;

    for (s32 count = ws->elemCount; count != 0; count--, slot += sizeof(POLY_GT4), el += ws->elemStride) {
        POLY_GT4* p = (POLY_GT4*)slot;
        u32 w0 = el[0], w1 = el[1], w2 = el[2], w3 = el[3];

        Psyz_GteDataWrite(6, colour);
        loadV(0, vertexAt(ws, w1 >> 16));
        Psyz_GteRtps();
        if (Psyz_GteReadflg() < 0) {
            continue;
        }
        storeXy(&p->x3, 14);

        loadV(0, vertexAt(ws, w1 & 0xFFFF));
        loadV(1, vertexAt(ws, w0 >> 16));
        loadV(2, vertexAt(ws, w0 & 0xFFFF));
        Psyz_GteRtpt();
        if (Psyz_GteReadflg() < 0) {
            continue;
        }
        storeXy(&p->x2, 12);
        storeXy(&p->x1, 13);
        storeXy(&p->x0, 14);
        Psyz_GteNclip();
        {
            s32 mac0 = (s32)Psyz_GteDataRead(24);
            /* Ordinary walk keeps MAC0 < 0 first, then MAC0 > 0 on the fallback; reversed flips both. */
            int firstOk = reverse ? mac0 > 0 : mac0 < 0;
            if (!firstOk) {
                Psyz_GteDataWrite(14, (u32)(u16)p->x3 | ((u32)(u16)p->y3 << 16));
                Psyz_GteNclip();
                mac0 = (s32)Psyz_GteDataRead(24);
                if (reverse ? mac0 >= 0 : mac0 <= 0) {
                    continue;
                }
            }
        }
        Psyz_GteAvsz4();
        loadV(0, normalAt(ws, w2 & 0xFFFF));
        loadV(1, normalAt(ws, w2 >> 16));
        loadV(2, normalAt(ws, w3 & 0xFFFF));
        Psyz_GteCommand(GTE_NCCT);
        storeRgb(&p->r0, 20);
        storeRgb(&p->r1, 21);
        storeRgb(&p->r2, 22);
        loadV(0, normalAt(ws, w3 >> 16));
        Psyz_GteCommand(GTE_NCCS);
        storeRgb(&p->r3, 22);
        linkPacket(ws, p, 12);
    }
    ws->primWrite = slot;
    return el;
}

u32* tmdDrawStreamGt4SemiTrans(TmdStreamWorkspace* ws, s32 flags, u32* el)
{
    return gt4(ws, el, 0x3E808080u, (flags & TMD_OBJECT_REVERSE_CULLING) != 0);
}

u32* tmdDrawStreamGt4(TmdStreamWorkspace* ws, s32 flags, u32* el)
{
    u32 colour = (flags & TMD_OBJECT_SEMI_TRANS) ? 0x3E808080u : 0x3C808080u;
    return gt4(ws, el, colour, (flags & TMD_OBJECT_REVERSE_CULLING) != 0);
}

/* Textured triangle, one face normal and a fixed material: one lit colour on all corners. */
static u32* gt3OneNormal(TmdStreamWorkspace* ws, u32* el, u32 colour)
{
    u8* slot = ws->primWrite;

    for (s32 count = ws->elemCount; count != 0; count--, slot += sizeof(POLY_GT3), el += ws->elemStride) {
        POLY_GT3* p = (POLY_GT3*)slot;
        u32 w0 = el[0], w1 = el[1];

        loadV(0, vertexAt(ws, w0 & 0xFFFF));
        loadV(1, vertexAt(ws, w0 >> 16));
        loadV(2, vertexAt(ws, w1 & 0xFFFF));
        Psyz_GteDataWrite(6, colour);
        Psyz_GteRtpt();
        if (Psyz_GteReadflg() < 0) {
            continue;
        }
        Psyz_GteNclip();
        if ((s32)Psyz_GteDataRead(24) <= 0) {
            continue;
        }
        storeXy(&p->x0, 12);
        storeXy(&p->x1, 13);
        storeXy(&p->x2, 14);
        Psyz_GteAvsz3();
        loadV(0, normalAt(ws, w1 >> 16));
        Psyz_GteCommand(GTE_NCCS);
        storeRgb(&p->r0, 22);
        storeRgb(&p->r1, 22);
        storeRgb(&p->r2, 22);
        linkPacket(ws, p, 9);
    }
    ws->primWrite = slot;
    return el;
}

u32* tmdDrawStreamPrimGt3OneNormal(TmdStreamWorkspace* ws, s32 flags, u32* el)
{
    (void)flags;
    return gt3OneNormal(ws, el, 0x34808080u);
}

u32* tmdDrawStreamPrimGt3OneNormalSemiTrans(TmdStreamWorkspace* ws, s32 flags, u32* el)
{
    (void)flags;
    return gt3OneNormal(ws, el, 0x36808080u);
}

static u32* gt4OneNormal(TmdStreamWorkspace* ws, u32* el, u32 colour)
{
    u8* slot = ws->primWrite;

    for (s32 count = ws->elemCount; count != 0; count--, slot += sizeof(POLY_GT4), el += ws->elemStride) {
        POLY_GT4* p = (POLY_GT4*)slot;
        u32 w0 = el[0], w1 = el[1], w2 = el[2];

        Psyz_GteDataWrite(6, colour);
        loadV(0, vertexAt(ws, w1 >> 16));
        Psyz_GteRtps();
        if (Psyz_GteReadflg() < 0) {
            continue;
        }
        storeXy(&p->x3, 14);

        loadV(0, vertexAt(ws, w1 & 0xFFFF));
        loadV(1, vertexAt(ws, w0 >> 16));
        loadV(2, vertexAt(ws, w0 & 0xFFFF));
        Psyz_GteRtpt();
        if (Psyz_GteReadflg() < 0) {
            continue;
        }
        storeXy(&p->x2, 12);
        storeXy(&p->x1, 13);
        storeXy(&p->x0, 14);
        Psyz_GteNclip();
        if ((s32)Psyz_GteDataRead(24) >= 0) {
            Psyz_GteDataWrite(14, (u32)(u16)p->x3 | ((u32)(u16)p->y3 << 16));
            Psyz_GteNclip();
            if ((s32)Psyz_GteDataRead(24) <= 0) {
                continue;
            }
        }
        Psyz_GteAvsz4();
        loadV(0, normalAt(ws, w2)); /* the whole word is the byte offset */
        Psyz_GteCommand(GTE_NCCS);
        storeRgb(&p->r0, 22);
        storeRgb(&p->r1, 22);
        storeRgb(&p->r2, 22);
        storeRgb(&p->r3, 22);
        linkPacket(ws, p, 12);
    }
    ws->primWrite = slot;
    return el;
}

u32* tmdDrawStreamPrimGt4OneNormal(TmdStreamWorkspace* ws, s32 flags, u32* el)
{
    (void)flags;
    return gt4OneNormal(ws, el, 0x3C808080u);
}

u32* tmdDrawStreamPrimGt4OneNormalSemiTrans(TmdStreamWorkspace* ws, s32 flags, u32* el)
{
    (void)flags;
    return gt4OneNormal(ws, el, 0x3E808080u);
}

/* Light one corner: material colour `colour` with normal `n`, result left in RGB2. */
static void lightCorner(const SVECTOR* n, u32 colour)
{
    Psyz_GteDataWrite(6, colour);
    loadV(0, n);
    Psyz_GteCommand(GTE_NCCS);
}

/* Textured triangle, a normal and a material colour per corner. Ignores reverse culling. */
u32* tmdDrawStreamPrimGt3CornerColors(TmdStreamWorkspace* ws, s32 flags, u32* el)
{
    u8* slot = ws->primWrite;
    u8  code = (flags & TMD_OBJECT_SEMI_TRANS) ? 0x36 : 0x34;

    for (s32 count = ws->elemCount; count != 0; count--, slot += sizeof(POLY_GT3), el += ws->elemStride) {
        POLY_GT3* p = (POLY_GT3*)slot;
        u32 w0 = el[0], w1 = el[1], w2 = el[2];

        loadV(0, vertexAt(ws, w0 & 0xFFFF));
        loadV(1, vertexAt(ws, w0 >> 16));
        loadV(2, vertexAt(ws, w1 & 0xFFFF));
        Psyz_GteRtpt();
        if (Psyz_GteReadflg() < 0) {
            continue;
        }
        Psyz_GteNclip();
        if ((s32)Psyz_GteDataRead(24) <= 0) {
            continue;
        }
        storeXy(&p->x0, 12);
        storeXy(&p->x1, 13);
        storeXy(&p->x2, 14);
        Psyz_GteAvsz3();
        lightCorner(normalAt(ws, w1 >> 16), el[3]);
        storeRgb(&p->r0, 22);
        lightCorner(normalAt(ws, w2 & 0xFFFF), el[4]);
        storeRgb(&p->r1, 22);
        lightCorner(normalAt(ws, w2 >> 16), el[5]);
        storeRgb(&p->r2, 22);
        p->code = code;
        linkPacket(ws, p, 9);
    }
    ws->primWrite = slot;
    return el;
}

/* Textured quad, a normal and a material colour per corner. */
u32* tmdDrawStreamPrimGt4CornerColors(TmdStreamWorkspace* ws, s32 flags, u32* el)
{
    u8* slot = ws->primWrite;
    u8  code = (flags & TMD_OBJECT_SEMI_TRANS) ? 0x3E : 0x3C;

    for (s32 count = ws->elemCount; count != 0; count--, slot += sizeof(POLY_GT4), el += ws->elemStride) {
        POLY_GT4* p = (POLY_GT4*)slot;
        u32 w0 = el[0], w1 = el[1], w2 = el[2], w3 = el[3];

        loadV(0, vertexAt(ws, w1 >> 16));
        Psyz_GteRtps();
        if (Psyz_GteReadflg() < 0) {
            continue;
        }
        storeXy(&p->x3, 14);

        loadV(0, vertexAt(ws, w1 & 0xFFFF));
        loadV(1, vertexAt(ws, w0 >> 16));
        loadV(2, vertexAt(ws, w0 & 0xFFFF));
        Psyz_GteRtpt();
        if (Psyz_GteReadflg() < 0) {
            continue;
        }
        storeXy(&p->x2, 12);
        storeXy(&p->x1, 13);
        storeXy(&p->x0, 14);
        Psyz_GteNclip();
        if ((s32)Psyz_GteDataRead(24) >= 0) {
            Psyz_GteDataWrite(14, (u32)(u16)p->x3 | ((u32)(u16)p->y3 << 16));
            Psyz_GteNclip();
            if ((s32)Psyz_GteDataRead(24) <= 0) {
                continue;
            }
        }
        Psyz_GteAvsz4();
        lightCorner(normalAt(ws, w2 & 0xFFFF), el[4]);
        storeRgb(&p->r0, 22);
        lightCorner(normalAt(ws, w2 >> 16), el[5]);
        storeRgb(&p->r1, 22);
        lightCorner(normalAt(ws, w3 & 0xFFFF), el[6]);
        storeRgb(&p->r2, 22);
        lightCorner(normalAt(ws, w3 >> 16), el[7]);
        storeRgb(&p->r3, 22);
        p->code = code;
        linkPacket(ws, p, 12);
    }
    ws->primWrite = slot;
    return el;
}
