/* Round-trips a hand-built LZSS stream through fsDecompressImagePayload and
 * the resumable fsDecompressStream (split at every input boundary). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "fs.h"
#include "port/psyq_compat.h"

extern u8 gFsLzssRing[256];

/* The decoder state normally lives in fs.c. */
u16 gFsLzssDecodeStatus; const u8* gFsLzssInputCursor; const u8* gFsLzssInputEnd;
u8* gFsPayloadWriteCursor; void* gFsLzssResumeAddress; u16 gFsLzssRingWriteIndex;
u16 gFsLzssUnreadBitCount; u16 gFsLzssInputByte; u16 gFsLzssUnreadBitMask;
s16 gFsLzssTokenValue; s16 gFsLzssTokenBitsNeeded; s32 gFsLzssMatchReadIndex;
/* The decoder bounds its output by these two objects (psyq_host.c, fs.c). */
unsigned char gHostRam[0x200000]; u_long _gFsImageUploadBuffer[0x460];

typedef struct { u8 buf[4096]; int nbits; } W;
static void put(W* w, unsigned v, int n)
{
    for (int i = n - 1; i >= 0; i--) {
        if (w->nbits % 8 == 0) w->buf[w->nbits / 8] = 0;
        if ((v >> i) & 1) w->buf[w->nbits / 8] |= 0x80 >> (w->nbits % 8);
        w->nbits++;
    }
}

int main(void)
{
    W w = {0};
    u8 expect[512]; int n = 0, ring = 1;
    u8 hist[256] = {0};
    const char* lit = "ABCDEFGH";
    for (int i = 0; i < 8; i++) { put(&w, 1, 1); put(&w, lit[i], 8); expect[n++] = lit[i]; hist[ring] = lit[i]; ring = (ring + 1) & 255; }
    /* match: index 1, length 6 (+2 = 8 bytes) copies "ABCDEFGH"; then overlap match */
    for (int rep = 0; rep < 2; rep++) {
        int idx = rep ? ring - 1 : 1, len = rep ? 12 : 6;
        put(&w, 0, 1); put(&w, idx, 8); put(&w, len, 4);
        for (int k = 0; k < len + 2; k++) { u8 b = hist[(idx + k) & 255]; expect[n++] = b; hist[ring] = b; ring = (ring + 1) & 255; }
    }
    put(&w, 0, 1); put(&w, 0, 8);
    int bytes = (w.nbits + 7) / 8 + 4;

    u8 out[512]; memset(out, 0xEE, sizeof out);
    gFsLzssInputCursor = w.buf; gFsPayloadWriteCursor = out;
    fsDecompressImagePayload();
    if (memcmp(out, expect, n)) { puts("payload FAIL"); return 1; }

    for (int split = 2; split < bytes - 1; split++) {
        memset(out, 0xEE, sizeof out);
        gFsLzssInputByte = 0; gFsLzssUnreadBitMask = 0; gFsLzssRingWriteIndex = 1;
        gFsLzssResumeAddress = NULL; gFsLzssDecodeStatus = FILE_SYSTEM_LZSS_DECODE_NEEDS_INPUT;
        gFsPayloadWriteCursor = out; gFsLzssInputCursor = w.buf; gFsLzssInputEnd = w.buf + split;
        fsDecompressStream();
        if (gFsLzssDecodeStatus == FILE_SYSTEM_LZSS_DECODE_NEEDS_INPUT) {
            /* feed the rest as a new interval: cursor was already advanced */
            gFsLzssInputEnd = w.buf + bytes + 64;
            fsDecompressStream();
        }
        if (gFsLzssDecodeStatus != FILE_SYSTEM_LZSS_DECODE_COMPLETE || memcmp(out, expect, n)) {
            printf("stream FAIL at split %d status %d\n", split, gFsLzssDecodeStatus); return 1;
        }
    }
    /* A stream decoding past the end of PS1 RAM drops the excess (ASan checks the
     * bytes after the array) and still consumes its input to the end marker. */
    u8* tail = gHostRam + sizeof(gHostRam) - 4;
    gFsLzssInputCursor = w.buf; gFsPayloadWriteCursor = tail;
    fsDecompressImagePayload();
    if (memcmp(tail, expect, 4) || gFsPayloadWriteCursor != tail || gFsLzssDecodeStatus != FILE_SYSTEM_LZSS_DECODE_COMPLETE) {
        puts("payload overrun FAIL"); return 1;
    }
    memset(tail, 0, 4);
    gFsLzssInputByte = 0; gFsLzssUnreadBitMask = 0; gFsLzssRingWriteIndex = 1;
    gFsLzssResumeAddress = NULL; gFsLzssDecodeStatus = FILE_SYSTEM_LZSS_DECODE_NEEDS_INPUT;
    gFsPayloadWriteCursor = tail; gFsLzssInputCursor = w.buf; gFsLzssInputEnd = w.buf + bytes + 64;
    fsDecompressStream();
    if (memcmp(tail, expect, 4) || gFsLzssDecodeStatus != FILE_SYSTEM_LZSS_DECODE_COMPLETE) {
        puts("stream overrun FAIL"); return 1;
    }
    /* A sector boundary after output has filled must not lose the bound on
     * resume. Exercise every input split, not just a single uninterrupted run. */
    for (int split = 2; split < bytes - 1; split++) {
        memset(tail, 0, 4);
        gFsLzssInputByte = 0; gFsLzssUnreadBitMask = 0; gFsLzssRingWriteIndex = 1;
        gFsLzssResumeAddress = NULL; gFsLzssDecodeStatus = FILE_SYSTEM_LZSS_DECODE_NEEDS_INPUT;
        gFsPayloadWriteCursor = tail; gFsLzssInputCursor = w.buf; gFsLzssInputEnd = w.buf + split;
        fsDecompressStream();
        if ((uintptr_t)gFsPayloadWriteCursor > (uintptr_t)(gHostRam + sizeof(gHostRam))) {
            printf("stream bound lost at split %d\n", split); return 1;
        }
        if (gFsLzssDecodeStatus == FILE_SYSTEM_LZSS_DECODE_NEEDS_INPUT) {
            gFsLzssInputEnd = w.buf + bytes + 64;
            fsDecompressStream();
        }
        if (memcmp(tail, expect, 4) || gFsLzssDecodeStatus != FILE_SYSTEM_LZSS_DECODE_COMPLETE ||
            gFsPayloadWriteCursor != gHostRam + sizeof(gHostRam)) {
            printf("stream overrun resume FAIL at split %d\n", split); return 1;
        }
    }
    puts("lzss OK");
    return 0;
}
