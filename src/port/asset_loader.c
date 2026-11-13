#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <mach-o/getsect.h>
#include <mach-o/dyld.h>
#endif

#include "port/asset_bind.h"
#include "port/asset_parts.h"
#include "port/disc_assets.h"
int peAssetsDump(const char* path);

#ifdef __APPLE__
extern const struct mach_header_64 _mh_execute_header;
static const PeAssetBinding* bindings(size_t* count)
{
    unsigned long size = 0;
    const uint8_t* p = getsectiondata(&_mh_execute_header, "__DATA", "__peassets", &size);
    *count = p ? size / sizeof(PeAssetBinding) : 0;
    return (const PeAssetBinding*)p;
}
#elif defined(_WIN32)
/* Definitions live in a separate translation unit. Keeping local zero-valued
 * markers here lets Clang fold accesses past the first marker to null pointers
 * instead of reading the records that the COFF linker places between them. */
extern const PeAssetBinding peAssetsBegin[], peAssetsEnd[];
static const PeAssetBinding* bindings(size_t* count)
{
    uintptr_t first = (uintptr_t)(peAssetsBegin + 1);
    *count = ((uintptr_t)peAssetsEnd - first) / sizeof(PeAssetBinding);
    return (const PeAssetBinding*)first;
}
#else
extern const PeAssetBinding __start_peassets[], __stop_peassets[];
static const PeAssetBinding* bindings(size_t* count)
{
    *count = (size_t)(__stop_peassets - __start_peassets);
    return __start_peassets;
}
#endif

/* Globals converted from the disassembly (src/port/asm_data.c) have their own sorted table; builds without that file
 * (the host tests) see an empty one. */
__attribute__((weak)) const PeAssetPart gPeAsmParts[1] = {{0}};
__attribute__((weak)) const int         gPeAsmPartCount = 0;

static const PeAssetPart* searchParts(const PeAssetPart* t, int count, const char* key)
{
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, c = strcmp(key, t[mid].key);
        if (c == 0) return &t[mid];
        if (c < 0) hi = mid - 1; else lo = mid + 1;
    }
    return NULL;
}

static const PeAssetPart* findPart(const char* key)
{
    const PeAssetPart* p = searchParts(gPeAssetParts, gPeAssetPartCount, key);
    return p ? p : searchParts(gPeAsmParts, gPeAsmPartCount, key);
}

int peAssetsLoad(struct PeDisc* disc)
{
    /* A cache belongs to this mounted image. Package IDs recur on the other disc. */
    uint8_t* pkgData[4096] = {0};
    size_t   pkgSize[4096] = {0};
    unsigned char attempted[4096] = {0};
    size_t          n;
    const PeAssetBinding* b = bindings(&n);
    static unsigned char* loaded;
    if (!loaded && n) {
        loaded = calloc(n, sizeof(*loaded));
        if (!loaded) {
            fprintf(stderr, "[assets] cannot allocate binding load state\n");
            return -1;
        }
    }
    int filled = 0, missing = 0, wrong = 0;

    for (size_t i = 0; i < n; i++) {
        /* Bindings live for the process lifetime. Models resolve their streams
         * in place and room tasks also mutate these arrays. Mounting another
         * disc must fill missing bindings without resetting that live state. */
        if (loaded[i]) continue;
        const PeAssetPart* part = findPart(b[i].key);
        if (!part) { fprintf(stderr, "[assets] no layout for %s\n", b[i].key); wrong++; continue; }
        size_t want = part->op == PE_ASSET_OP_GRID_TABLE ? (size_t)part->size / 4 * sizeof(void*) : part->size;
        if (part->op == PE_ASSET_OP_DECODE) want = b[i].size;          /* checked below against the record count */
        if (part->op == PE_ASSET_OP_DECODE && (!part->load || part->size % part->load || !b[i].aux ||
                                                b[i].size % (part->size / part->load))) {
            fprintf(stderr, "[assets] %s: %u bytes of %u-byte records do not fit a %zu-byte array\n", b[i].key,
                    (unsigned)part->size, (unsigned)part->load, b[i].size);
            wrong++;
            continue;
        }
        if (want != b[i].size) {
            fprintf(stderr, "[assets] %s: array is %zu bytes, layout says %zu\n", b[i].key, b[i].size, want);
            wrong++;
            continue;
        }
        int pi = part->package;
        /* Missing packages are common on the other disc. Cache misses only
         * for this mount, so each absent package is scanned once and retried
         * when the next disc is mounted. */
        if (!attempted[pi]) {
            attempted[pi] = 1;
            const PeAssetPackage* pk = &gPeAssetPackages[pi];
            pkgData[pi] = pk->stage < 0 ? peDiscReadFile(disc, "SLUS_010.42", &pkgSize[pi])
                                        : peDiscReadPackage(disc, pk->stage, pk->folder, pk->file, &pkgSize[pi]);
        }
        if (!pkgData[pi]) { missing++; continue; }
        if ((size_t)part->offset + part->size > pkgSize[pi]) {
            fprintf(stderr, "[assets] %s: past the end of %s\n", b[i].key, gPeAssetPackages[pi].name);
            wrong++;
            continue;
        }
        const uint8_t* src = pkgData[pi] + part->offset;
        if (part->op == PE_ASSET_OP_COPY) {
            memcpy(b[i].dst, src, part->size);
        } else if (part->op == PE_ASSET_OP_DECODE) {
            size_t count = part->size / part->load, hostElem = b[i].size / count;
            (void)hostElem;
            ((PeAssetDecode)b[i].aux)(b[i].dst, src, count);
        } else {
            const uint32_t* words = (const uint32_t*)(const void*)src; /* PS1 addresses; little endian host */
            const int16_t*  cells = (const int16_t*)b[i].aux;
            const int16_t** out = (const int16_t**)b[i].dst;
            for (uint32_t k = 0; k < part->size / 4; k++) {
                uint32_t w;
                memcpy(&w, &words[k], 4);
                out[k] = w ? cells + (w - part->load - part->cellsOffset) / 2 : NULL;
            }
        }
        loaded[i] = 1;
        filled++;
    }
    if (wrong) fprintf(stderr, "[assets] %d bindings could not be filled\n", wrong);
    if (missing) fprintf(stderr, "[assets] %d bindings wait for the other disc\n", missing);
    for (size_t i = 0; i < 4096; i++) free(pkgData[i]);
    return filled;
}

/* Development check: one line per binding, "<key> <hash of the bytes>". Grid tables hash their cell indices, not
 * addresses. The dump is a fingerprint of what the disc filled in. */
int peAssetsDump(const char* path)
{
    size_t n;
    const PeAssetBinding* b = bindings(&n);
    FILE* f = fopen(path, "w");
    if (!f) return 0;
    for (size_t i = 0; i < n; i++) {
        uint64_t h = 1469598103934665603ull;
        const PeAssetPart* part = findPart(b[i].key);
        if (part && part->op == PE_ASSET_OP_GRID_TABLE) {
            const int16_t** t = (const int16_t**)b[i].dst;
            for (size_t k = 0; k < b[i].size / sizeof(void*); k++) {
                long v = t[k] ? (long)(t[k] - (const int16_t*)b[i].aux) : -1;
                h = (h ^ (uint64_t)v) * 1099511628211ull;
            }
        } else if (part && part->op == PE_ASSET_OP_DECODE) {
            /* Decoded structs hold pointers, whose values differ between two executables. Hash each 8-byte word:
             * a pointer into another bound array as (that array's key, offset), any other non-small word that
             * looks like an address as a marker, everything else as it is. */
            const uint8_t* d = b[i].dst;
            for (size_t k = 0; k + 8 <= b[i].size; k += 8) {
                uint64_t w;
                memcpy(&w, d + k, 8);
                uint64_t token = w;
                if (w > 0xFFFFFFFFull) {
                    token = 0x7074720000000000ull; /* an address outside every bound array */
                    for (size_t j = 0; j < n; j++) {
                        uintptr_t lo = (uintptr_t)b[j].dst;
                        if (w >= lo && w < lo + b[j].size) {
                            uint64_t kh = 1469598103934665603ull;
                            for (const char* c = b[j].key; *c; c++) kh = (kh ^ (uint8_t)*c) * 1099511628211ull;
                            token = kh ^ (w - lo) << 1;
                            break;
                        }
                    }
                }
                h = (h ^ token) * 1099511628211ull;
            }
            for (size_t k = b[i].size & ~(size_t)7; k < b[i].size; k++) h = (h ^ d[k]) * 1099511628211ull;
        } else {
            const uint8_t* d = b[i].dst;
            for (size_t k = 0; k < b[i].size; k++) h = (h ^ d[k]) * 1099511628211ull;
        }
        fprintf(f, "%s %016llx\n", b[i].key, (unsigned long long)h);
        if (getenv("PE2_DUMP_KEY") && strstr(b[i].key, getenv("PE2_DUMP_KEY"))) {   /* development: raw bytes of one array */
            char name[512];
            snprintf(name, sizeof name, "%s.%s.bin", path, strrchr(b[i].key, '/') ? strrchr(b[i].key, '/') + 1 : b[i].key);
            FILE* g = fopen(name, "wb");
            if (g) { fwrite(b[i].dst, 1, b[i].size, g); fclose(g); }
        }
    }
    fclose(f);
    return 1;
}
