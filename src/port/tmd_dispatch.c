/*
 * Portable replacements for the handwritten TMD stream walk
 * (hasm/Tmd_DispatchStream.s, hasm/tmdSkipStreamRecord.s) and the handler id
 * registry that stands in for the 32-bit code addresses stored in streams.
 */
#include "tmd.h"
#include "main/tmd_types.h"
#include "main/coord.h"

#include <psyz/gte.h>
#include "port/model_cull.h"

enum { MAX_HANDLERS = 256 };

typedef u32* (*TmdHandler)(TmdStreamWorkspace*, s32, u32*);

static TmdHandler sHandlers[MAX_HANDLERS];
static u32        sHandlerCount;

u32 tmdHandlerToId(void* handler)
{
    for (u32 i = 0; i < sHandlerCount; i++) {
        if ((void*)sHandlers[i] == handler) {
            return i + 1;
        }
    }
    if (sHandlerCount == MAX_HANDLERS) {
        return 0;
    }
    sHandlers[sHandlerCount] = (TmdHandler)handler;
    return ++sHandlerCount;
}

void* tmdHandlerFromId(u32 id)
{
    return (id != 0 && id <= sHandlerCount) ? (void*)sHandlers[id - 1] : (void*)0;
}

u32* tmdSkipStreamRecord(TmdStreamWorkspace* ws, s32 flags, u32* stream)
{
    (void)flags;
    return stream + (u32)ws->elemStride * (u32)ws->elemCount;
}

/* Record: { opcode, handler, count << 16 | stride }, then count * stride words.
 * Stops at TMD_STREAM_GROUP_END (not consumed) and returns that cursor. */
u32* Tmd_DispatchStream(TmdStreamWorkspace* ws, s32 flags, u32* stream)
{
    ws->dispatchObjectFlags = flags;
    while (stream[0] != (u32)-2) {
        u32 opcode     = stream[0];
        u32 id         = stream[1];
        u32 dimensions = stream[2];
        TmdHandler handler = (TmdHandler)tmdHandlerFromId(id);

        ws->opcode     = opcode;
        ws->elemCount  = (s32)(dimensions >> 16);
        ws->elemStride = (s32)(dimensions & 0xFFFF);
        stream += 3;
        stream = handler(ws, ws->dispatchObjectFlags, stream);
    }
    return stream;
}

/* ---------------------------------------------------------------------------
 * tmdDrawModelStream (hasm/tmdDrawModelStream.s)
 *
 * Per command group (one per skeletal part): install the part's rotation in the
 * GTE rotation matrix, build the light matrix L = B * R (B = the view-light
 * rotation in the workspace, R the part rotation) from its transpose, load the
 * part translation, then dispatch the group's records.
 * ------------------------------------------------------------------------ */

#define GTE_MVMVA_LLM_V0_NOTRANS 0x4A4A6012u /* sf=1, mx=light, v=V0, cv=none, lm=0 */

static u32 pack16(s16 lo, s16 hi)
{
    return (u32)(u16)lo | ((u32)(u16)hi << 16);
}

static void tmdInstallPartTransform(TmdStreamWorkspace* ws, const GfxCoord* part)
{
    const s16 (*r)[3] = part->workm.m;
    const s16 (*b)[3] = ws->viewLightRotation;
    s16 row[3][3]; /* row i = R^T * b_i, the rows of B * R */

    /* Rotation matrix = part rotation. */
    Psyz_GteCtrlWrite(0, pack16(r[0][0], r[0][1]));
    Psyz_GteCtrlWrite(1, pack16(r[0][2], r[1][0]));
    Psyz_GteCtrlWrite(2, pack16(r[1][1], r[1][2]));
    Psyz_GteCtrlWrite(3, pack16(r[2][0], r[2][1]));
    Psyz_GteCtrlWrite(4, (u32)(u16)r[2][2]);

    /* Light matrix = transpose of the part rotation (the product below uses it). */
    Psyz_GteCtrlWrite(8, pack16(r[0][0], r[1][0]));
    Psyz_GteCtrlWrite(9, pack16(r[2][0], r[0][1]));
    Psyz_GteCtrlWrite(10, pack16(r[1][1], r[2][1]));
    Psyz_GteCtrlWrite(11, pack16(r[0][2], r[1][2]));
    Psyz_GteCtrlWrite(12, (u32)(u16)r[2][2]);

    for (int i = 0; i < 3; i++) {
        Psyz_GteDataWrite(0, pack16(b[i][0], b[i][1]));
        Psyz_GteDataWrite(1, (u32)(u16)b[i][2]);
        Psyz_GteCommand(GTE_MVMVA_LLM_V0_NOTRANS);
        row[i][0] = (s16)Psyz_GteDataRead(9);
        row[i][1] = (s16)Psyz_GteDataRead(10);
        row[i][2] = (s16)Psyz_GteDataRead(11);
    }

    /* Light matrix = B * R; translation = part translation. */
    Psyz_GteCtrlWrite(8, pack16(row[0][0], row[0][1]));
    Psyz_GteCtrlWrite(9, pack16(row[0][2], row[1][0]));
    Psyz_GteCtrlWrite(10, pack16(row[1][1], row[1][2]));
    Psyz_GteCtrlWrite(11, pack16(row[2][0], row[2][1]));
    Psyz_GteCtrlWrite(12, (u32)(u16)row[2][2]);
    Psyz_GteCtrlWrite(5, (u32)part->workm.t[0]);
    Psyz_GteCtrlWrite(6, (u32)part->workm.t[1]);
    Psyz_GteCtrlWrite(7, (u32)part->workm.t[2]);
}

void tmdDrawModelStream(TmdStreamWorkspace* ws, u32 objectFlags, u32* stream, TmdObject* model)
{
    s32       parts = model->partCount;
    GfxCoord* part  = model->coords;

    peModelCullBegin();
    for (;;) {
        u32 word = *stream;
        if (word == (u32)-1) { /* TMD_STREAM_END */
            peModelCullEnd();
            return;
        }
        if (parts > 0 && word != (u32)-2) {
            tmdInstallPartTransform(ws, part);
        }
        stream = Tmd_DispatchStream(ws, (s32)objectFlags, stream);
        stream++; /* consume the group marker; empty groups still advance the part */
        parts--;
        part++;
    }
}
