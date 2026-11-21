/* Bounds of a requested game location against the stage directories (include/port/location_check.h). */
#include "port/location_check.h"

#include <stdio.h>

static const char* const sStageNames[PE_LOCATION_STAGE_COUNT] = {
    "Akropolis", "Dryfield", "Dryfield (night)", "Mine/Shelter", "Shelter/Neo Ark",
};

static unsigned _peLocationCount(const PeLocationTables* tables, const void* array, size_t elementSize)
{
    size_t bytes = array ? tables->arrayBytes(array) : 0;
    return elementSize ? (unsigned)(bytes / elementSize) : 0;
}

PeLocationStatus peLocationCheck(const PeLocationTables* tables, unsigned stage, unsigned area, unsigned room,
                                 unsigned warp, PeLocationReport* report)
{
    PeLocationReport local = { PE_LOCATION_OK, 0, 0, 0 };
    const void*      warps;
    const void*      rooms;

    if (stage < 1 || stage > PE_LOCATION_STAGE_COUNT) {
        local.status = PE_LOCATION_BAD_STAGE;
        goto done;
    }
    local.areaCount = tables->areaCount[stage - 1];
    if (area != 0 && area == tables->debugArea[stage - 1]) {
        local.status = PE_LOCATION_DEBUG_ROOM;
        goto done;
    }
    if (area < 1 || area > local.areaCount) {
        local.status = PE_LOCATION_BAD_AREA;
        goto done;
    }
    warps = tables->warpArray(stage, area);
    if (warps == NULL) {
        local.status = PE_LOCATION_NO_AREA;
        goto done;
    }
    local.warpCount = _peLocationCount(tables, warps, tables->warpEntrySize);
    rooms           = tables->roomArray(stage, area);
    /* An area without collision rooms still starts in room 1; its other per-room tables have one entry. */
    local.roomCount = rooms ? _peLocationCount(tables, rooms, tables->roomSize) : 1;
    if (local.warpCount == 0 || local.roomCount == 0) {
        local.status = PE_LOCATION_UNKNOWN_EXTENT;
        goto done;
    }
    if (room < 1 || room > local.roomCount) {
        local.status = PE_LOCATION_BAD_ROOM;
        goto done;
    }
    if (warp < 1 || warp > local.warpCount) {
        local.status = PE_LOCATION_BAD_WARP;
        goto done;
    }
done:
    if (report) {
        *report = local;
    }
    return local.status;
}

int peLocationDescribe(const PeLocationReport* report, unsigned stage, unsigned area, unsigned room, unsigned warp,
                       char* buffer, size_t size)
{
    const char* stageName = (stage >= 1 && stage <= PE_LOCATION_STAGE_COUNT) ? sStageNames[stage - 1] : "?";

    switch (report->status) {
        case PE_LOCATION_OK:
            return snprintf(buffer, size, "stage %u (%s) area %u room %u warp %u is valid", stage, stageName, area, room, warp);
        case PE_LOCATION_DEBUG_ROOM:
            return snprintf(buffer, size, "stage %u (%s) area %u is the debug room; it has no warp or room tables", stage, stageName, area);
        case PE_LOCATION_BAD_STAGE:
            return snprintf(buffer, size, "stage %u does not exist (stages are 1-%d)", stage, PE_LOCATION_STAGE_COUNT);
        case PE_LOCATION_BAD_AREA:
            return snprintf(buffer, size, "stage %u (%s) has areas 1-%u; area %u does not exist", stage, stageName, report->areaCount, area);
        case PE_LOCATION_NO_AREA:
            return snprintf(buffer, size, "stage %u (%s) area %u is unused: the game has no warp table for it", stage, stageName, area);
        case PE_LOCATION_UNKNOWN_EXTENT:
            return snprintf(buffer, size, "stage %u (%s) area %u: the size of its warp or room table is unknown to the port (or the disc holding it is not loaded)",
                            stage, stageName, area);
        case PE_LOCATION_BAD_ROOM:
            return snprintf(buffer, size, "stage %u (%s) area %u has room states 1-%u; room state %u is out of range", stage, stageName, area,
                            report->roomCount, room);
        case PE_LOCATION_BAD_WARP:
            return snprintf(buffer, size, "stage %u (%s) area %u has warp entries 1-%u; warp %u is out of range", stage, stageName, area,
                            report->warpCount, warp);
    }
    return snprintf(buffer, size, "unknown location status %d", (int)report->status);
}
