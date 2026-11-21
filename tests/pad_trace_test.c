#include "port/pad_trace.h"
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static void setVar(const char* name, const char* value)
{
#ifdef _WIN32
    CHECK(_putenv_s(name, value ? value : "") == 0);
#else
    CHECK(value ? setenv(name, value, 1) == 0 : unsetenv(name) == 0);
#endif
}
static int varIs(const char* name, const char* value)
{
    const char* actual = getenv(name);
    return value ? actual && strcmp(actual, value) == 0 : !actual || !*actual;
}
/* Replays and recordings must not depend on wall time: emulated clocks by default, explicit choices kept. */
static void checkDefaultClocks(void)
{
    const char* vars[] = {"PE2_PAD_REPLAY", "PE2_PAD_RECORD", "PSYZ_AUDIO_CLOCK", "PSYZ_RASTER_CLOCK"};
    for (unsigned i = 0; i < 4; ++i) setVar(vars[i], NULL);
    CHECK(pePadTraceDefaultClocks() == 0);
    CHECK(varIs("PSYZ_AUDIO_CLOCK", NULL) && varIs("PSYZ_RASTER_CLOCK", NULL));
    setVar("PE2_PAD_REPLAY", "route.pad");
    CHECK(pePadTraceDefaultClocks() == (PE_PAD_CLOCK_AUDIO | PE_PAD_CLOCK_RASTER));
    CHECK(varIs("PSYZ_AUDIO_CLOCK", "video") && varIs("PSYZ_RASTER_CLOCK", "virtual"));
    CHECK(pePadTraceDefaultClocks() == 0); /* idempotent */
    setVar("PE2_PAD_REPLAY", NULL); setVar("PSYZ_RASTER_CLOCK", NULL);
    setVar("PE2_PAD_RECORD", "new.pad"); setVar("PSYZ_AUDIO_CLOCK", "host");
    CHECK(pePadTraceDefaultClocks() == PE_PAD_CLOCK_RASTER);
    CHECK(varIs("PSYZ_AUDIO_CLOCK", "host") && varIs("PSYZ_RASTER_CLOCK", "virtual"));
    for (unsigned i = 0; i < 4; ++i) setVar(vars[i], NULL);
}
static FILE* textFile(const char* text)
{
    FILE* f = tmpfile(); CHECK(f); CHECK(fputs(text, f) >= 0); rewind(f); return f;
}
int main(void)
{
    FILE* f = tmpfile(); CHECK(f); CHECK(pePadTraceHeader(f, 1));
    /* Digital and analog replies, including high-bit bytes and axis extremes. */
    const unsigned char samples[][8] = {{0,0x41,0xf7,0xff,0,0,0,0}, {0,0x73,0xff,0xbf,0,255,128,127}, {0,0x73,0xff,0xff,128,128,128,128}};
    for (unsigned i=0; i<3; i++) CHECK(pePadTraceWrite(f, i, (const char*)samples[i], 8));
    rewind(f); CHECK(pePadTraceHeader(f, 0));
    char out[8];
    for (unsigned i=0; i<3; i++) { CHECK(pePadTraceRead(f, i, out, 8) == 1); CHECK(memcmp(out, samples[i], 8) == 0); }
    CHECK(pePadTraceRead(f, 3, out, 8) == 0); fclose(f);
    const char* invalid[] = {"0 0041ffff0000000g\n", "1 0041ffff00000000\n", "0 0041ffff00000000", "0 0041ff\n", "0 0041ffff0000000000\n", "-0 0041ffff00000000\n", "18446744073709551616 0041ffff00000000\n", "0 0041ffff00000000 extra\n"};
    for (unsigned i=0; i<sizeof invalid/sizeof invalid[0]; i++) {
        f=textFile(invalid[i]); memset(out, 0x55, sizeof out);
        CHECK(pePadTraceRead(f, 0, out, sizeof out) == -1);
        for (unsigned j=0; j<sizeof out; j++) CHECK(out[j] == 0x55);
        fclose(f);
    }
    f=textFile("PE2PAD 2\n"); CHECK(!pePadTraceHeader(f,0)); fclose(f);
    uint32_t count = 17; uint16_t mask = 23;
    CHECK(pePadControlParse("advance 45 0x2010\n", &count, &mask) == 1);
    CHECK(count == 45 && mask == 0x2010);
    CHECK(pePadControlParse("advance 4294967295 65535", &count, &mask) == 1);
    CHECK(count == UINT32_MAX && mask == UINT16_MAX);
    const char* badControl[] = {"advance 0 0", "advance -1 0", "advance 4294967296 0",
        "advance 1 -1", "advance 1 65536", "advance 1 0x", "advance 1 0 garbage",
        "advance 1", "advance 1 +1", "quit extra", "advance 1 08", ""};
    for (unsigned i = 0; i < sizeof badControl / sizeof badControl[0]; ++i) {
        count = 17; mask = 23;
        CHECK(pePadControlParse(badControl[i], &count, &mask) == -1);
        CHECK(count == 17 && mask == 23);
    }
    CHECK(pePadControlParse("quit\n", &count, &mask) == 0);
    CHECK(count == 17 && mask == 23);
    checkDefaultClocks();
    puts("pad trace: exact digital/analog samples, frame order, EOF, malformed input and replay clock defaults OK");
}
