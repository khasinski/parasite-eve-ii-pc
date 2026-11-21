/* Development locations are checked against the warp and room tables before room start indexes them unchecked.
 * Regression: PE2_START_LOCATION=1,16,2,1,4,1 (Akropolis helicopter landing pad, whose warp table has three
 * entries) crashed room start with SIGSEGV reading a fourth entry past the table. */
#include "port/location_check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

typedef struct { char bytes[56]; } FakeWarp;
typedef struct { void* pointers[4]; } FakeRoom;

/* Stage 1: 20 areas; area 16 = helipad (3 warps, 1 room), area 3 = patio (4 warps, 3 rooms), area 7 has no table,
 * area 9 borrows an array whose extent is unknown. Stage 2: area 1 with warps but no collision rooms. */
static FakeWarp sHelipadWarps[3], sPatioWarps[4], sUnknownWarps[2], sDryfieldWarps[5];
static FakeRoom sHelipadRooms[1], sPatioRooms[3];

static const void* fakeWarpArray(unsigned stage, unsigned area)
{
    if (stage == 1 && area == 16) return sHelipadWarps;
    if (stage == 1 && area == 3) return sPatioWarps;
    if (stage == 1 && area == 9) return sUnknownWarps;
    if (stage == 2 && area == 1) return sDryfieldWarps;
    return NULL;
}

static const void* fakeRoomArray(unsigned stage, unsigned area)
{
    if (stage == 1 && area == 16) return sHelipadRooms;
    if (stage == 1 && area == 3) return sPatioRooms;
    if (stage == 1 && area == 9) return sHelipadRooms;
    return NULL;
}

static size_t fakeArrayBytes(const void* array)
{
    if (array == sHelipadWarps) return sizeof(sHelipadWarps);
    if (array == sPatioWarps) return sizeof(sPatioWarps);
    if (array == sDryfieldWarps) return sizeof(sDryfieldWarps);
    if (array == sHelipadRooms) return sizeof(sHelipadRooms);
    if (array == sPatioRooms) return sizeof(sPatioRooms);
    return 0;
}

static const PeLocationTables sTables = {
    .areaCount     = { 20, 38, 38, 49, 33 },
    .debugArea     = { 21, 0, 0, 0, 0 },
    .warpArray     = fakeWarpArray,
    .roomArray     = fakeRoomArray,
    .arrayBytes    = fakeArrayBytes,
    .warpEntrySize = sizeof(FakeWarp),
    .roomSize      = sizeof(FakeRoom),
};

static PeLocationStatus check(unsigned stage, unsigned area, unsigned room, unsigned warp, PeLocationReport* report)
{
    PeLocationReport local;
    PeLocationStatus status = peLocationCheck(&sTables, stage, area, room, warp, report ? report : &local);
    CHECK(status == (report ? report : &local)->status);
    CHECK(peLocationCheck(&sTables, stage, area, room, warp, NULL) == status);
    return status;
}

int main(void)
{
    PeLocationReport report;
    char             text[256];

    /* The helipad's three warps are valid; 4 and 5 (the reported crash) and 0 are not. */
    for (unsigned warp = 1; warp <= 3; warp++) CHECK(check(1, 16, 1, warp, NULL) == PE_LOCATION_OK);
    CHECK(check(1, 16, 1, 4, &report) == PE_LOCATION_BAD_WARP);
    CHECK(report.warpCount == 3 && report.roomCount == 1 && report.areaCount == 20);
    peLocationDescribe(&report, 1, 16, 1, 4, text, sizeof(text));
    CHECK(strstr(text, "Akropolis") && strstr(text, "area 16") && strstr(text, "warp entries 1-3") && strstr(text, "warp 4"));
    CHECK(check(1, 16, 1, 5, NULL) == PE_LOCATION_BAD_WARP);
    CHECK(check(1, 16, 1, 0, NULL) == PE_LOCATION_BAD_WARP);
    CHECK(check(1, 16, 1, 256, NULL) == PE_LOCATION_BAD_WARP); /* not truncated to a byte */

    /* Room states index the area's collision rooms. */
    CHECK(check(1, 16, 3, 1, &report) == PE_LOCATION_BAD_ROOM);
    peLocationDescribe(&report, 1, 16, 3, 1, text, sizeof(text));
    CHECK(strstr(text, "room states 1-1") && strstr(text, "room state 3"));
    CHECK(check(1, 3, 3, 4, NULL) == PE_LOCATION_OK);
    CHECK(check(1, 3, 4, 1, NULL) == PE_LOCATION_BAD_ROOM);
    CHECK(check(1, 3, 0, 1, NULL) == PE_LOCATION_BAD_ROOM);
    /* An area without collision rooms has room 1 only. */
    CHECK(check(2, 1, 1, 5, &report) == PE_LOCATION_OK && report.roomCount == 1 && report.warpCount == 5);
    CHECK(check(2, 1, 2, 1, NULL) == PE_LOCATION_BAD_ROOM);

    /* Stages, areas, empty slots and the debug room. */
    CHECK(check(0, 1, 1, 1, NULL) == PE_LOCATION_BAD_STAGE);
    CHECK(check(6, 1, 1, 1, &report) == PE_LOCATION_BAD_STAGE && report.areaCount == 0);
    CHECK(check(1, 0, 1, 1, NULL) == PE_LOCATION_BAD_AREA);
    CHECK(check(1, 22, 1, 1, &report) == PE_LOCATION_BAD_AREA);
    peLocationDescribe(&report, 1, 22, 1, 1, text, sizeof(text));
    CHECK(strstr(text, "areas 1-20"));
    CHECK(check(2, 39, 1, 1, NULL) == PE_LOCATION_BAD_AREA);
    CHECK(check(1, 21, 9, 9, NULL) == PE_LOCATION_DEBUG_ROOM);
    CHECK(check(2, 21, 1, 1, NULL) == PE_LOCATION_NO_AREA);
    CHECK(check(1, 7, 1, 1, NULL) == PE_LOCATION_NO_AREA);

    /* A borrowed array of unknown size is refused rather than guessed. */
    CHECK(check(1, 9, 1, 1, &report) == PE_LOCATION_UNKNOWN_EXTENT);
    peLocationDescribe(&report, 1, 9, 1, 1, text, sizeof(text));
    CHECK(strstr(text, "unknown"));

    /* Every status describes itself, and the text fits small buffers. */
    for (int status = PE_LOCATION_OK; status <= PE_LOCATION_BAD_WARP; status++) {
        PeLocationReport any = { (PeLocationStatus)status, 20, 1, 3 };
        char             small[16];
        CHECK(peLocationDescribe(&any, 1, 16, 1, 4, text, sizeof(text)) > 0 && text[0]);
        peLocationDescribe(&any, 1, 16, 1, 4, small, sizeof(small));
        CHECK(strlen(small) < sizeof(small));
    }
    puts("location_check_test: ok");
    return 0;
}
