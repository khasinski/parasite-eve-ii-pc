/*
 * Pre-transformed untextured Gouraud draw handlers
 * (hasm/Tmd_StreamHandler_Prim32.s, hasm/Tmd_StreamHandler_Prim3A.s).
 *
 * Packets are PsyZ POLY_G3 / POLY_G4 slots (host layout: pointer-width tag), and
 * the ordering table is an array of PsyZ OT_TYPE entries, so `workspace->ot` is
 * indexed in OT_TYPE units here. Packet cursors and the depth cache are in the
 * workspace as on the console.
 */
#include "tmd.h"
#include "main/tmd_types.h"

#include <psyz.h>
#include <libgpu.h>
#include <libgte.h>
#include <psyz/gte.h>
#include "port/gte_compat.h"
#include "port/model_cull.h"

enum {
    DEPTH_MASK  = 0x3FFF,
    DEPTH_SHIFT = 4,
};

static u32 packXy(short x, short y)
{
    return (u32)(u16)x | ((u32)(u16)y << 16);
}

static s32 cachedDepth(const TmdStreamWorkspace* ws, u32 byteOffset)
{
    return *(const s32*)((const u8*)ws->szTable + byteOffset);
}

/* Prepend `packet` to the OT bucket chosen by OTZ (already averaged). */
static void linkPacket(TmdStreamWorkspace* ws, void* packet, u32 length, u32 code)
{
    u32 otz    = Psyz_GteDataRead(7);
    u32 bucket = ((otz << (ws->otDepthShift & 31)) & DEPTH_MASK) >> DEPTH_SHIFT;
    OT_TYPE* head = (OT_TYPE*)ws->ot + bucket;

    setlen(packet, length);
    setcode(packet, code);
    addPrim(head, packet);
    peModelCullLinked(head, packet);
}

static u32* g3PreXform(TmdStreamWorkspace* ws, u32* el, u32 code)
{
    u8* slot = ws->preXformWrite;

    for (s32 count = ws->elemCount; count != 0; count--, slot += sizeof(POLY_G3), el += ws->elemStride) {
        POLY_G3* p = (POLY_G3*)slot;
        u32 w0 = el[0], w1 = el[1];

        /* Facing test on the packet's corners. */
        gte_ldSXYP(packXy(p->x0, p->y0));
        gte_ldSXYP(packXy(p->x1, p->y1));
        gte_ldSXYP(packXy(p->x2, p->y2));
        Psyz_GteNclip();
        if ((s32)Psyz_GteDataRead(24) <= 0) {
            continue;
        }
        s32 d0 = cachedDepth(ws, w0 & 0xFFFF);
        s32 d1 = cachedDepth(ws, w0 >> 16);
        s32 d2 = cachedDepth(ws, w1 & 0xFFFF);
        if (d0 < 0 || d1 < 0 || d2 < 0) {
            continue;
        }
        Psyz_GteDataWrite(17, (u32)d0);
        Psyz_GteDataWrite(18, (u32)d1);
        Psyz_GteDataWrite(19, (u32)d2);
        Psyz_GteAvsz3();
        linkPacket(ws, p, 6, code);
    }
    ws->preXformWrite = slot;
    return el;
}

static u32* g4PreXform(TmdStreamWorkspace* ws, u32* el, u32 code)
{
    u8* slot = ws->preXformWrite;

    for (s32 count = ws->elemCount; count != 0; count--, slot += sizeof(POLY_G4), el += ws->elemStride) {
        POLY_G4* p = (POLY_G4*)slot;
        u32 w0 = el[0], w1 = el[1];

        gte_ldSXYP(packXy(p->x0, p->y0));
        gte_ldSXYP(packXy(p->x1, p->y1));
        gte_ldSXYP(packXy(p->x2, p->y2));
        Psyz_GteNclip();
        if ((s32)Psyz_GteDataRead(24) <= 0) {
            /* Fallback: accept the quad if triangle (1, 2, 3) is wound negatively. */
            gte_ldSXYP(packXy(p->x3, p->y3));
            Psyz_GteNclip();
            if ((s32)Psyz_GteDataRead(24) >= 0) {
                continue;
            }
        }
        s32 d0 = cachedDepth(ws, w0 & 0xFFFF);
        s32 d1 = cachedDepth(ws, w0 >> 16);
        s32 d2 = cachedDepth(ws, w1 & 0xFFFF);
        s32 d3 = cachedDepth(ws, w1 >> 16);
        if (d0 < 0 || d1 < 0 || d2 < 0 || d3 < 0) {
            continue;
        }
        Psyz_GteDataWrite(16, (u32)d0);
        Psyz_GteDataWrite(17, (u32)d1);
        Psyz_GteDataWrite(18, (u32)d2);
        Psyz_GteDataWrite(19, (u32)d3);
        Psyz_GteAvsz4();
        linkPacket(ws, p, 8, code);
    }
    ws->preXformWrite = slot;
    return el;
}

u32* Tmd_StreamHandler_Prim32(TmdStreamWorkspace* ws, s32 flags, u32* el) { (void)flags; return g3PreXform(ws, el, 0x32); }
u32* tmdDrawStreamPrimG3PreXform(TmdStreamWorkspace* ws, s32 flags, u32* el) { (void)flags; return g3PreXform(ws, el, 0x30); }
u32* Tmd_StreamHandler_Prim3A(TmdStreamWorkspace* ws, s32 flags, u32* el) { (void)flags; return g4PreXform(ws, el, 0x3A); }
u32* tmdDrawStreamPrimG4PreXform(TmdStreamWorkspace* ws, s32 flags, u32* el) { (void)flags; return g4PreXform(ws, el, 0x38); }

/* ---- Pre-transformed Gouraud textured triangles and quads ----------------
 * Same structure as the untextured pre-transformed handlers above, but the
 * packets are POLY_GT3 / POLY_GT4 (UVs, CLUT and colours already filled by the
 * vertex pre-pass); only the command byte is stamped. `flags` selects the
 * semi-transparent command and the facing rule. */

enum { OBJ_SEMI_TRANS = 0x02, OBJ_REVERSE_CULLING = 0x10 };

static u32* gt3PreXform(TmdStreamWorkspace* ws, u32* el, u32 code, int reverse)
{
    u8* slot = ws->preXformWrite;

    for (s32 count = ws->elemCount; count != 0; count--, slot += sizeof(POLY_GT3), el += ws->elemStride) {
        POLY_GT3* p = (POLY_GT3*)slot;
        u32 w0 = el[0], w1 = el[1];

        gte_ldSXYP(packXy(p->x0, p->y0));
        gte_ldSXYP(packXy(p->x1, p->y1));
        gte_ldSXYP(packXy(p->x2, p->y2));
        Psyz_GteNclip();
        {
            s32 mac0 = (s32)Psyz_GteDataRead(24);
            if (reverse ? mac0 >= 0 : mac0 <= 0) {
                continue;
            }
        }
        s32 d0 = cachedDepth(ws, w0 & 0xFFFF);
        s32 d1 = cachedDepth(ws, w0 >> 16);
        s32 d2 = cachedDepth(ws, w1 & 0xFFFF);
        if (d0 < 0 || d1 < 0 || d2 < 0) {
            continue;
        }
        Psyz_GteDataWrite(17, (u32)d0);
        Psyz_GteDataWrite(18, (u32)d1);
        Psyz_GteDataWrite(19, (u32)d2);
        Psyz_GteAvsz3();
        linkPacket(ws, p, 9, code);
    }
    ws->preXformWrite = slot;
    return el;
}

static u32* gt4PreXform(TmdStreamWorkspace* ws, u32* el, u32 code, int reverse)
{
    u8* slot = ws->preXformWrite;

    for (s32 count = ws->elemCount; count != 0; count--, slot += sizeof(POLY_GT4), el += ws->elemStride) {
        POLY_GT4* p = (POLY_GT4*)slot;
        u32 w0 = el[0], w1 = el[1];

        gte_ldSXYP(packXy(p->x0, p->y0));
        gte_ldSXYP(packXy(p->x1, p->y1));
        gte_ldSXYP(packXy(p->x2, p->y2));
        Psyz_GteNclip();
        {
            s32 mac0 = (s32)Psyz_GteDataRead(24);
            int firstOk = reverse ? mac0 < 0 : mac0 > 0;
            if (!firstOk) {
                gte_ldSXYP(packXy(p->x3, p->y3));
                Psyz_GteNclip();
                mac0 = (s32)Psyz_GteDataRead(24);
                if (reverse ? mac0 <= 0 : mac0 >= 0) {
                    continue;
                }
            }
        }
        s32 d0 = cachedDepth(ws, w0 & 0xFFFF);
        s32 d1 = cachedDepth(ws, w0 >> 16);
        s32 d2 = cachedDepth(ws, w1 & 0xFFFF);
        s32 d3 = cachedDepth(ws, w1 >> 16);
        if (d0 < 0 || d1 < 0 || d2 < 0 || d3 < 0) {
            continue;
        }
        Psyz_GteDataWrite(16, (u32)d0);
        Psyz_GteDataWrite(17, (u32)d1);
        Psyz_GteDataWrite(18, (u32)d2);
        Psyz_GteDataWrite(19, (u32)d3);
        Psyz_GteAvsz4();
        linkPacket(ws, p, 12, code);
    }
    ws->preXformWrite = slot;
    return el;
}

u32* tmdDrawStreamPrimGt3PreXformSemiTrans(TmdStreamWorkspace* ws, s32 flags, u32* el)
{
    return gt3PreXform(ws, el, 0x36, (flags & OBJ_REVERSE_CULLING) != 0);
}

u32* tmdDrawStreamPrimGt3PreXform(TmdStreamWorkspace* ws, s32 flags, u32* el)
{
    return gt3PreXform(ws, el, (flags & OBJ_SEMI_TRANS) ? 0x36 : 0x34, (flags & OBJ_REVERSE_CULLING) != 0);
}

u32* tmdDrawStreamPrimGt4PreXformSemiTrans(TmdStreamWorkspace* ws, s32 flags, u32* el)
{
    return gt4PreXform(ws, el, 0x3E, (flags & OBJ_REVERSE_CULLING) != 0);
}

u32* tmdDrawStreamPrimGt4PreXform(TmdStreamWorkspace* ws, s32 flags, u32* el)
{
    return gt4PreXform(ws, el, (flags & OBJ_SEMI_TRANS) ? 0x3E : 0x3C, (flags & OBJ_REVERSE_CULLING) != 0);
}
