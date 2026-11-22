/*
 * Portable replacements for the handwritten LZSS decoders
 * (src/main/hasm/fsDecompressStream.s, fsDecompressImagePayload.s).
 *
 * Format (see src/main/fs.h): bits are read MSB first. A 1 flag is followed by
 * an 8-bit literal; a 0 flag by an 8-bit absolute index into a 256-byte history
 * ring (0 ends the stream) and a 4-bit length, giving matches of length+2 bytes.
 * The ring write index starts at 1. Every literal or index byte read fetches
 * the following input byte eagerly, even when it is aligned, which fixes where
 * a sector-fed stream can suspend; that behaviour is kept so streams that
 * straddle sector boundaries suspend at the same points as on the console.
 */
#include "fs.h"

#include <stdint.h>
#include <stdio.h>

#include "port/psyq_compat.h"

/* Saved token state; defined in fs.c, private to the decoder. */
extern s16 gFsLzssTokenValue;
extern s16 gFsLzssTokenBitsNeeded;
extern s32 gFsLzssMatchReadIndex;

/* The PlayStation kept the ring in scratchpad bytes [0, 256). */
u8 gFsLzssRing[256];

enum { RING_MASK = 255, MIN_MATCH_EXTRA = 1 };

extern u_long _gFsImageUploadBuffer[0x460];

/* End of the object the cursor writes into: PS1 RAM or the image upload buffer. A
 * damaged stream may decode past it; those bytes are dropped (and reported once)
 * while the input is still consumed exactly as before. */
static const u8* _lzssOutputLimit(const u8* out)
{
    const u8* ram    = gHostRam;
    const u8* upload = (const u8*)_gFsImageUploadBuffer;
    if (out >= ram && out < ram + sizeof(gHostRam)) {
        return ram + sizeof(gHostRam);
    }
    if (out >= upload && out < upload + sizeof(_gFsImageUploadBuffer)) {
        return upload + sizeof(_gFsImageUploadBuffer);
    }
    return NULL;
}

static void _lzssReportOverrun(void)
{
    static int reported;
    if (!reported) {
        reported = 1;
        fprintf(stderr, "[fs] compressed data decodes past its destination; the excess is dropped\n");
    }
}

#define PUT(b)                                     \
    do {                                           \
        if (limit == NULL || out < limit) {        \
            *out = (b);                            \
        } else if (out == limit) {                 \
            _lzssReportOverrun();                  \
        }                                          \
        out++;                                     \
    } while (0)

void fsDecompressStream(void)
{
    const u8* cur = gFsLzssInputCursor;
    const u8* end = gFsLzssInputEnd;
    u8*       out = gFsPayloadWriteCursor;
    u32       mask = gFsLzssUnreadBitMask;  /* mask of unread bits in `byte` */
    u32       byte = gFsLzssInputByte;
    u32       ring = gFsLzssRingWriteIndex;
    s32       bits = (s16)gFsLzssUnreadBitCount;
    s32       tok  = gFsLzssTokenValue;
    s32       need = gFsLzssTokenBitsNeeded;
    s32       idx  = gFsLzssMatchReadIndex;
    s32       len;
    u32       a;
    uintptr_t resume = (uintptr_t)gFsLzssResumeAddress;
    const u8* limit  = _lzssOutputLimit(out);

    gFsLzssResumeAddress = NULL;

/* Fetch a byte and suspend if the advanced cursor reaches the end. */
#define REFILL(n)                    \
    do {                             \
        mask = 0xFF;                 \
        bits = 8;                    \
        byte = *cur++;               \
        if (cur == end) {            \
            resume = (n) + 1;        \
            goto save;               \
        }                            \
    } while (0)

    switch (resume) {
    case 1: goto r0;
    case 2: goto r1;
    case 3: goto r2;
    case 4: goto r3;
    case 5: goto r4;
    case 6: goto r5;
    case 7: goto r6;
    default: break;
    }

read_flag:
    if (mask == 0) {
        REFILL(0);
    }
r0:
    tok = mask & byte;
    mask >>= 1;
    bits--;
    tok &= (mask ^ 0xFF);
    need = 8;
    if (tok <= 0) {
        goto read_match_index;
    }
    tok = 0;
    if (mask == 0) {
        REFILL(1);
    }
r1:
    tok = mask & byte;
    need -= bits;
    REFILL(2);
r2:
    if (need != 0) {
        mask >>= need;
        bits -= need;
        a = (mask ^ 0xFF) & byte;
        tok <<= need;
        a >>= bits;
        tok |= (s32)a;
    }
    gFsLzssRing[ring] = (u8)tok;
    PUT((u8)tok);
    ring = (ring + 1) & RING_MASK;
    goto read_flag;

read_match_index:
    idx = 0;
    if (mask == 0) {
        REFILL(3);
    }
r3:
    idx = mask & byte;
    need -= bits;
    REFILL(4);
r4:
    if (need != 0) {
        mask >>= need;
        bits -= need;
        a = (mask ^ 0xFF) & byte;
        idx <<= need;
        a >>= bits;
        idx |= (s32)a;
    }
    if (idx == 0) {
        goto complete;
    }
    need = 4;
    tok  = 0;
    if (mask == 0) {
        REFILL(5);
    }
r5:
    len = need - bits;
    if (len > 0) {
        need = len;
        tok  = mask & byte;
        REFILL(6);
    }
r6:
    a = mask & byte;
    mask >>= need;
    bits -= need;
    a &= (mask ^ 0xFF);
    tok <<= need;
    a >>= bits;
    tok |= (s32)a;
    len = tok + MIN_MATCH_EXTRA;
    do {
        u8 b = gFsLzssRing[idx & RING_MASK];
        gFsLzssRing[ring] = b;
        PUT(b);
        ring = (ring + 1) & RING_MASK;
        idx++;
    } while (--len >= 0);
    goto read_flag;

complete:
    gFsLzssDecodeStatus = FILE_SYSTEM_LZSS_DECODE_COMPLETE;
save:
    gFsLzssResumeAddress    = (void*)resume;
    gFsLzssInputCursor      = cur;
    gFsPayloadWriteCursor   = out;
    gFsLzssUnreadBitMask    = (u16)mask;
    gFsLzssInputByte        = (u16)byte;
    gFsLzssRingWriteIndex   = (u16)ring;
    gFsLzssUnreadBitCount   = (u16)bits;
    gFsLzssTokenValue       = (s16)tok;
    gFsLzssTokenBitsNeeded  = (s16)need;
    gFsLzssMatchReadIndex   = idx;
#undef REFILL
}

void fsDecompressImagePayload(void)
{
    const u8* cur  = gFsLzssInputCursor;
    u8*       out  = gFsPayloadWriteCursor;
    u32       mask = 0;
    u32       byte = 0;
    s32       bits = 0;
    u32       ring = 1;
    const u8* limit = _lzssOutputLimit(out);

    /* Read n bits (n <= 8), MSB first, eagerly fetching the next byte after a
     * full value as the handwritten routine does. */
    for (;;) {
        s32 v, need, idx;
        u32 a;
        if (mask == 0) {
            mask = 0xFF;
            bits = 8;
            byte = *cur++;
        }
        v = mask & byte;
        mask >>= 1;
        bits--;
        v &= (mask ^ 0xFF);
        need = 8;
        if (v > 0) {
            if (mask == 0) {
                mask = 0xFF; bits = 8; byte = *cur++;
            }
            v = mask & byte;
            need -= bits;
            mask = 0xFF; bits = 8; byte = *cur++;
            if (need != 0) {
                mask >>= need; bits -= need;
                a = ((mask ^ 0xFF) & byte) >> bits;
                v = (v << need) | (s32)a;
            }
            gFsLzssRing[ring] = (u8)v;
            PUT((u8)v);
            ring = (ring + 1) & RING_MASK;
            continue;
        }
        idx = 0;
        if (mask == 0) {
            mask = 0xFF; bits = 8; byte = *cur++;
        }
        idx = mask & byte;
        need -= bits;
        mask = 0xFF; bits = 8; byte = *cur++;
        if (need != 0) {
            mask >>= need; bits -= need;
            a = ((mask ^ 0xFF) & byte) >> bits;
            idx = (idx << need) | (s32)a;
        }
        if (idx == 0) {
            break;
        }
        need = 4;
        v    = 0;
        if (mask == 0) {
            mask = 0xFF; bits = 8; byte = *cur++;
        }
        if (need - bits > 0) {
            need -= bits;
            v = mask & byte;
            mask = 0xFF; bits = 8; byte = *cur++;
        }
        a = mask & byte;
        mask >>= need;
        bits -= need;
        a = (a & (mask ^ 0xFF)) >> bits;
        v = (v << need) | (s32)a;
        for (s32 len = v + MIN_MATCH_EXTRA; len >= 0; len--) {
            u8 b = gFsLzssRing[idx & RING_MASK];
            gFsLzssRing[ring] = b;
            PUT(b);
            ring = (ring + 1) & RING_MASK;
            idx++;
        }
    }
    gFsLzssDecodeStatus = FILE_SYSTEM_LZSS_DECODE_COMPLETE;
}
