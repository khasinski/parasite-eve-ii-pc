/* Include the production reader with allocation fault injection: failed malloc
 * and realloc must return a failed asset load, not crash or lose an allocation. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int failAllocation, failGrowth, liveAllocations;
static void* checkedMalloc(size_t size)
{
    if (failAllocation) return NULL;
    void* p = malloc(size);
    if (p) ++liveAllocations;
    return p;
}
static void* checkedRealloc(void* p, size_t size)
{
    if (failGrowth) return NULL;
    return realloc(p, size);
}
static void* checkedCalloc(size_t count, size_t size)
{
    if (failAllocation) return NULL;
    void* p = calloc(count, size);
    if (p) ++liveAllocations;
    return p;
}
static void checkedFree(void* p) { if (p) --liveAllocations; free(p); }
#define malloc checkedMalloc
#define realloc checkedRealloc
#define calloc checkedCalloc
#define free checkedFree
#include "../src/port/disc_assets.c"
#undef malloc
#undef realloc
#undef calloc
#undef free

static unsigned char image[64 * SECTOR_RAW];
static unsigned char* sector(unsigned lba) { return image + lba * SECTOR_RAW + MODE2_USER_OFFSET; }
static void word(unsigned char* p, uint32_t n)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (unsigned char)(n >> (8 * i));
}
static size_t record(unsigned char* p, const char* name, unsigned lba, unsigned size)
{
    size_t length = strlen(name), bytes = (33 + length + 1) & ~(size_t)1;
    p[0] = (unsigned char)bytes; p[32] = (unsigned char)length;
    word(p + 2, lba); word(p + 10, size); memcpy(p + 33, name, length);
    return bytes;
}
static void fixture(void)
{
    memset(image, 0, sizeof image);
    unsigned char* p = sector(16); p[0] = 1; memcpy(p + 1, "CD001", 5);
    word(p + 158, 20); word(p + 166, SECTOR_USER);
    p = sector(20);
    p += record(p, "STAGE1.CDF;1", 30, 5 * SECTOR_USER);
    record(p, "EXTRA.BIN;1", 40, 2051);
    word(sector(30), 101); word(sector(30) + 4, 4);
    word(sector(31), 7); word(sector(31) + 4, 1);
    p = sector(32); p[0] = 0; p[1] = 0xff; p[3] = 8; word(p + 4, 1);
    /* Literal 'B' followed by EOS; padding is trimmed by readFilePackage. */
    p[16] = 0xa1;
    memset(sector(40), 0x5a, SECTOR_USER); memset(sector(41), 0xc3, 3);
}
static PeDisc* mount(const char* bin, const char* cue)
{
    FILE* f = fopen(bin, "wb"); assert(f);
    assert(fwrite(image, 1, sizeof image, f) == sizeof image); assert(!fclose(f));
    PeDisc* d = peDiscOpen(cue); assert(d); return d;
}
static void release(PeDisc* d) { peDiscClose(d); }
int main(void)
{
    char root[] = "/tmp/pe2-disc-read-XXXXXX"; assert(mkdtemp(root));
    char bin[512], cue[512];
    snprintf(bin, sizeof bin, "%s/game.bin", root); snprintf(cue, sizeof cue, "%s/game.cue", root);
    FILE* f = fopen(cue, "w"); assert(f); fputs("FILE game.bin BINARY\n", f); assert(!fclose(f));
    fixture(); PeDisc* d = mount(bin, cue); size_t size = 99;
    unsigned char* out = peDiscReadPackage(d, 1, 101, 7, &size);
    assert(out && size == 1 && out[0] == 'B'); checkedFree(out);
    out = peDiscReadFile(d, "EXTRA.BIN", &size);
    assert(out && size == 2051);
    for (unsigned i = 0; i < size; ++i) assert(out[i] == (i < 2048 ? 0x5a : 0xc3));
    checkedFree(out);
    /* Both the compressed staging allocation and standalone ISO read can fail. */
    failAllocation = 1; size = 99;
    assert(!peDiscReadPackage(d, 1, 101, 7, &size) && size == 0);
    size = 99; assert(!peDiscReadFile(d, "EXTRA.BIN", &size) && size == 0);
    failAllocation = 0; release(d);

    /* CDF counts and offsets must not wrap back into unrelated sectors or
     * cause allocations based on sizes larger than the mounted image. */
    for (unsigned kind = 0; kind < 6; ++kind) {
        fixture();
        if (kind == 0) word(sector(32) + 4, UINT32_MAX);
        if (kind == 1) word(sector(30) + 4, UINT32_MAX);
        if (kind == 2) word(sector(31) + 4, UINT32_MAX);
        if (kind == 3) word(sector(31) + 4, 4); /* first sector outside folder */
        if (kind == 4) word(sector(16) + 166, UINT32_MAX);
        if (kind == 5) word(sector(32) + 4, 4); /* crosses into next folder */
        d = mount(bin, cue); size = 99;
        assert(!peDiscReadPackage(d, 1, 101, 7, &size) && size == 0); release(d);
    }
    fixture(); record(sector(20), "EXTRA.BIN;1", 40, UINT32_MAX);
    d = mount(bin, cue); size = 99;
    assert(!peDiscReadFile(d, "EXTRA.BIN", &size) && size == 0); release(d);

    /* >1 MiB of zero literals forces decoder growth; failed realloc must
     * release the old buffer. Successful growth preserves every output byte. */
    size_t literals = (1u << 20) + 17, bits = literals * 9 + 9, bytes = (bits + 7) / 8;
    unsigned char* compressed = calloc(bytes, 1); assert(compressed);
    for (size_t i = 0; i < literals; ++i) compressed[(i * 9) / 8] |= (unsigned char)(128u >> ((i * 9) % 8));
    failAllocation = 1; size = 99;
    assert(!peLzssDecode(compressed, bytes, &size) && size == 0 && liveAllocations == 0);
    failAllocation = 0; failGrowth = 1; size = 99;
    assert(!peLzssDecode(compressed, bytes, &size) && size == 0 && liveAllocations == 0);
    failGrowth = 0; out = peLzssDecode(compressed, bytes, &size);
    assert(out && size == literals);
    for (size_t i = 0; i < size; ++i) assert(out[i] == 0);
    checkedFree(out); free(compressed);
    assert(!peDiscReadFile(NULL, "EXTRA.BIN", &size) && size == 0);
    assert(!peDiscReadPackage(NULL, 1, 101, 7, &size) && size == 0);
    assert(!peLzssDecode(NULL, 1, &size) && size == 0);
    assert(!peLzssDecode(NULL, 0, NULL));
    assert(liveAllocations == 0);
    assert(!unlink(bin)); assert(!unlink(cue)); assert(!rmdir(root));
    puts("Disc reads: complete assets, invalid extents and allocation failures checked");
    return 0;
}
