#include "port/disc_assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

enum { SECTOR_USER = 2048, SECTOR_RAW = 2352, MODE2_USER_OFFSET = 24 };

struct PeDisc {
    FILE*    bin;
    long     sectorSize;
    long     userOffset;
    uint32_t cdfStart[6]; /* absolute sector of STAGE<n>.CDF, 0 if absent */
    uint32_t hedStart;
    uint8_t* hed;         /* STAGE0.HED contents (0x1B80 bytes) */
};

static int readSector(PeDisc* d, uint32_t lba, uint8_t* out)
{
    if (fseek(d->bin, (long)lba * d->sectorSize + d->userOffset, SEEK_SET) != 0) return 0;
    return fread(out, 1, SECTOR_USER, d->bin) == SECTOR_USER;
}

static uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t rd32le(const uint8_t* p) { return rd32(p); }

static int cueBinPath(const char* cue, char* out, size_t cap)
{
    FILE* f = fopen(cue, "r");
    char  line[1024];
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        char* start = line;
        while (isspace((unsigned char)*start)) start++;
        char* q = strchr(start, '"');
        char* e = q ? strchr(q + 1, '"') : NULL;
        if (strncmp(start, "FILE", 4) == 0 && q && e) {
            const char* slash = strrchr(cue, '/');
            const char* backslash = strrchr(cue, '\\');
            if (backslash && (!slash || backslash > slash)) slash = backslash;
            size_t      dir = slash ? (size_t)(slash - cue + 1) : 0;
            *e = 0;
            if (q[1] == '/' || q[1] == '\\' || (q[1] && q[2] == ':')) dir = 0;
            if (dir + strlen(q + 1) + 1 > cap) { fclose(f); return 0; }
            memcpy(out, cue, dir);
            strcpy(out + dir, q + 1);
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

/* Finds a file in the ISO 9660 root directory (names compared without the ";1" version suffix). */
static int isoFind(PeDisc* d, const char* name, uint32_t* lba, uint32_t* size)
{
    uint8_t pvd[SECTOR_USER], dir[SECTOR_USER];
    if (!readSector(d, 16, pvd) || memcmp(pvd + 1, "CD001", 5) != 0) return 0;
    uint32_t rootLba = rd32le(pvd + 156 + 2), rootSize = rd32le(pvd + 156 + 10);
    for (uint32_t s = 0; s * SECTOR_USER < rootSize; s++) {
        if (!readSector(d, rootLba + s, dir)) return 0;
        for (size_t o = 0; o < SECTOR_USER && dir[o];) {
            size_t len = dir[o];
            if (len < 34 || len > SECTOR_USER - o) return 0;
            size_t nl = dir[o + 32];
            if (nl > len - 33) return 0;
            char   n[64] = {0};
            if (nl < sizeof n) {
                memcpy(n, dir + o + 33, nl);
                char* semi = strchr(n, ';');
                if (semi) *semi = 0;
                if (strcmp(n, name) == 0) {
                    *lba = rd32le(dir + o + 2);
                    *size = rd32le(dir + o + 10);
                    return 1;
                }
            }
            o += len;
        }
    }
    return 0;
}

PeDisc* peDiscOpen(const char* cuePath)
{
    char    bin[1024];
    PeDisc* d = calloc(1, sizeof *d);
    uint32_t lba, size;
    if (!d || !cueBinPath(cuePath, bin, sizeof bin) || !(d->bin = fopen(bin, "rb"))) { free(d); return NULL; }
    d->sectorSize = SECTOR_RAW;
    d->userOffset = MODE2_USER_OFFSET;
    for (int n = 0; n <= 5; n++) {
        char name[16];
        snprintf(name, sizeof name, "STAGE%d.CDF", n);
        if (isoFind(d, name, &lba, &size)) d->cdfStart[n] = lba;
    }
    if (isoFind(d, "STAGE0.HED", &lba, &size)) {
        d->hed = malloc(4 * SECTOR_USER);
        if (!d->hed) { peDiscClose(d); return NULL; }
        for (int i = 0; i < 4; i++) {
            if (!readSector(d, lba + i, d->hed + i * SECTOR_USER)) {
                peDiscClose(d);
                return NULL;
            }
        }
    }
    return d;
}

int peDiscNumber(PeDisc* d)
{
    uint32_t lba, size;
    if (!d) return 0;
    if (d->cdfStart[1] && d->cdfStart[2] &&
        isoFind(d, "PE_DISK.01", &lba, &size) &&
        isoFind(d, "SLUS_010.42", &lba, &size)) return 1;
    if (d->cdfStart[4] && d->cdfStart[5] &&
        isoFind(d, "PE_DISK.02", &lba, &size) &&
        isoFind(d, "SLUS_010.55", &lba, &size)) return 2;
    return 0;
}

void peDiscClose(PeDisc* d)
{
    if (!d) return;
    if (d->bin) fclose(d->bin);
    free(d->hed);
    free(d);
}

/* Reads the first package chunk of the file starting at `sector` (absolute). */
static uint8_t* readFilePackage(PeDisc* d, uint32_t sector, size_t* size)
{
    for (int guard = 0; guard < 64; guard++) {
        uint8_t  head[SECTOR_USER];
        if (!readSector(d, sector, head)) return NULL;
        uint8_t  type = head[0], end = head[1];
        uint32_t sectorLen = head[2] | head[3] << 8, count = rd32(head + 4);
        if (sectorLen <= 0x10 || sectorLen > SECTOR_USER || count == 0) return NULL;
        if (type == 0) {
            size_t   cap = (size_t)count * SECTOR_USER, n = 0;
            uint8_t* raw = malloc(cap);
            for (uint32_t i = 0; i < count; i++) {
                uint8_t sec[SECTOR_USER];
                if (!readSector(d, sector + i, sec)) { free(raw); return NULL; }
                size_t from = i == 0 ? 0x10 : 0;
                memcpy(raw + n, sec + from, sectorLen - from);
                n += sectorLen - from;
            }
            while (n > 0 && raw[n - 1] == 0) n--; /* the encoder pads with zero bytes after the end marker */
            uint8_t* out = peLzssDecode(raw, n, size);
            free(raw);
            return out;
        }
        if (end == 0xFF) return NULL;
        sector += count;
    }
    return NULL;
}

uint8_t* peDiscReadFile(PeDisc* d, const char* name, size_t* size)
{
    uint32_t lba, bytes;
    if (!isoFind(d, name, &lba, &bytes)) return NULL;
    uint8_t* out = malloc(((size_t)bytes + SECTOR_USER - 1) / SECTOR_USER * SECTOR_USER);
    for (uint32_t i = 0; i * SECTOR_USER < bytes; i++)
        if (!readSector(d, lba + i, out + (size_t)i * SECTOR_USER)) { free(out); return NULL; }
    *size = bytes;
    return out;
}

uint8_t* peDiscReadPackage(PeDisc* d, int stage, int folder, int file, size_t* size)
{
    if (stage < 0 || stage > 5 || !d->cdfStart[stage]) return NULL;
    if (stage == 0) {
        if (!d->hed) return NULL;
        for (size_t o = 0x78; o + 8 <= 0x1B80; o += 8) { /* 3 stream records, then the file list */
            uint32_t id = rd32(d->hed + o), off = rd32(d->hed + o + 4);
            if (id == 0xFFFFFFFFu) break;
            if (id == (uint32_t)file) return readFilePackage(d, d->cdfStart[0] + off, size);
        }
        return NULL;
    }
    uint8_t  list[SECTOR_USER];
    uint32_t base = d->cdfStart[stage] + 1; /* the first folder starts at sector 1 */
    if (!readSector(d, d->cdfStart[stage], list)) return NULL;
    for (int i = 0; i < 256; i++) {
        uint32_t id = rd32(list + i * 8), count = rd32(list + i * 8 + 4);
        if (count == 0) break;
        if (id == (uint32_t)folder) {
            uint8_t head[SECTOR_USER];
            if (!readSector(d, base, head)) return NULL;
            for (int k = 0; k < 0xA2; k++) {
                uint32_t fid = rd32(head + k * 8), off = rd32(head + k * 8 + 4);
                if (off == 0) break;
                if (fid == (uint32_t)file) return readFilePackage(d, base + off, size);
            }
            return NULL;
        }
        base += count;
    }
    return NULL;
}

/* MSB-first bit reader; matches tools/peassets/lzss.py decode_lzss (ring starts zeroed, offset 0 ends). */
uint8_t* peLzssDecode(const uint8_t* in, size_t inSize, size_t* outSize)
{
    size_t   cap = 1 << 20, n = 0, pos = 0;
    uint8_t* out = malloc(cap);
    uint8_t  ring[256] = {0};
    unsigned ringPos = 0, mask = 0, buf = 0;

#define GETBITS(count, dst)                                   \
    do {                                                      \
        unsigned v_ = 0;                                      \
        for (int b_ = 0; b_ < (count); b_++) {                \
            if (mask == 0) {                                  \
                buf = pos < inSize ? in[pos++] : 0;           \
                mask = 128;                                   \
            }                                                 \
            v_ = (v_ << 1) | ((buf & mask) != 0);             \
            mask >>= 1;                                       \
        }                                                     \
        (dst) = v_;                                           \
    } while (0)
#define EMIT(byte)                                            \
    do {                                                      \
        if (n == cap) out = realloc(out, cap *= 2);           \
        ring[ringPos] = (byte);                               \
        ringPos = (ringPos + 1) & 0xFF;                       \
        out[n++] = (byte);                                    \
    } while (0)

    for (;;) {
        unsigned flag, v;
        if (pos >= inSize && mask == 0) break; /* ran out of input without an end marker */
        GETBITS(1, flag);
        if (flag) {
            GETBITS(8, v);
            EMIT((uint8_t)v);
        } else {
            unsigned off, len;
            GETBITS(8, off);
            if (off == 0) break;
            GETBITS(4, len);
            off -= 1;
            for (unsigned k = 0; k < len + 2; k++) {
                uint8_t byte = ring[off];
                EMIT(byte);
                off = (off + 1) & 0xFF;
            }
        }
    }
    *outSize = n;
    return out;
}
