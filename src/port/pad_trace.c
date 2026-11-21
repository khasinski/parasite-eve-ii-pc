#include "port/pad_trace.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#define PAD_TRACE_MAX_BYTES 256
int pePadTraceHeader(FILE* file, int writing)
{
    const char magic[] = "PE2PAD 1\n";
    char actual[sizeof magic];
    if (writing) return fputs(magic, file) >= 0 && fflush(file) == 0;
    return fgets(actual, sizeof actual, file) && strcmp(actual, magic) == 0;
}
int pePadTraceWrite(FILE* file, uint64_t index, const char* pad, size_t length)
{
    if (!length || length > PAD_TRACE_MAX_BYTES) return 0;
    if (fprintf(file, "%" PRIu64 " ", index) < 0) return 0;
    for (size_t i = 0; i < length; ++i)
        if (fprintf(file, "%02x", (unsigned char)pad[i]) < 0) return 0;
    return fputc('\n', file) != EOF && fflush(file) == 0;
}
static int hexDigit(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
int pePadTraceRead(FILE* file, uint64_t index, char* pad, size_t length)
{
    char line[32 + 2 * PAD_TRACE_MAX_BYTES], decoded[PAD_TRACE_MAX_BYTES];
    if (!length || length > PAD_TRACE_MAX_BYTES) return -1;
    if (!fgets(line, sizeof line, file)) return feof(file) && !ferror(file) ? 0 : -1;
    if (line[0] < '0' || line[0] > '9') return -1;
    char* bytes;
    errno = 0;
    uint64_t actual = strtoull(line, &bytes, 10);
    if (errno || actual != index || *bytes++ != ' ') return -1;
    if (strlen(bytes) != length * 2 + 1 || bytes[length * 2] != '\n') return -1;
    for (size_t i = 0; i < length; ++i) {
        int hi = hexDigit(bytes[i * 2]), lo = hexDigit(bytes[i * 2 + 1]);
        if (hi < 0 || lo < 0) return -1;
        decoded[i] = (char)((hi << 4) | lo);
    }
    memcpy(pad, decoded, length);
    return 1;
}

int pePadControlParse(const char* line, uint32_t* count, uint16_t* mask)
{
    if (!line || !count || !mask) return -1;
    if (strncmp(line, "quit", 4) == 0) {
        const char* end = line + 4;
        while (isspace((unsigned char)*end)) ++end;
        return *end ? -1 : 0;
    }
    if (strncmp(line, "advance ", 8) != 0) return -1;
    const char* start = line + 8;
    while (isspace((unsigned char)*start)) ++start;
    if (*start < '0' || *start > '9') return -1;
    char* end;
    errno = 0;
    unsigned long long duration = strtoull(start, &end, 10);
    if (errno || !duration || duration > UINT32_MAX || !isspace((unsigned char)*end)) return -1;
    start = end;
    while (isspace((unsigned char)*start)) ++start;
    if (*start < '0' || *start > '9') return -1;
    errno = 0;
    unsigned long buttons = strtoul(start, &end, 0);
    if (errno || buttons > UINT16_MAX) return -1;
    while (isspace((unsigned char)*end)) ++end;
    if (*end) return -1;
    *count = (uint32_t)duration;
    *mask = (uint16_t)buttons;
    return 1;
}

static int setDefault(const char* name, const char* value)
{
    const char* current = getenv(name);
    if (current && *current) return 0;
#ifdef _WIN32
    return _putenv_s(name, value) == 0;
#else
    return setenv(name, value, 1) == 0;
#endif
}

int pePadTraceDefaultClocks(void)
{
    const char *replay = getenv("PE2_PAD_REPLAY"), *record = getenv("PE2_PAD_RECORD");
    if (!(replay && *replay) && !(record && *record)) return 0;
    int set = 0;
    if (setDefault("PSYZ_AUDIO_CLOCK", "video")) set |= PE_PAD_CLOCK_AUDIO;
    if (setDefault("PSYZ_RASTER_CLOCK", "virtual")) set |= PE_PAD_CLOCK_RASTER;
    return set;
}
