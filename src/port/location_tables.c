/* The game's stage directories for location validation (include/port/location_check.h). */
#include "common.h"
#include "gameplay/collision.h"
#include "gameplay/direction.h"
#include "gameplay/room.h"
#include "../gameplay/loading.h"
#include "mapui/map_dryfield.h"
#include "mapui/map_dryfield_full.h"
#include "mapui/map_neo_ark.h"
#include "mapui/map_shelter.h"
#include "main/areas.h"
#include "port/asset_bind.h"
#include "port/location_check.h"
#include "stages/akropolis/stage.h"

#include <stdio.h>
#include <stdlib.h>

static const void* _peLocationWarpArray(unsigned stage, unsigned area)
{
    return gDirectionAreaWarpEntriesByStage[stage - 1][area - 1];
}

static const void* _peLocationRoomArray(unsigned stage, unsigned area)
{
    const WorldCollisionStageResources* resources = gWorldCollisionStageResources[stage - 1];
    return resources->areaRooms ? resources->areaRooms[area - 1] : NULL;
}

/* Static room arrays of the Shelter and Neo Ark maps (src/mapui). */
extern const PeLocationArrayExtent gMapShelterLocationExtents[], gMapNeoArkLocationExtents[];
extern const int                   gMapShelterLocationExtentCount, gMapNeoArkLocationExtentCount;

/* Akropolis arrays come from the disc (their binding records the size); the other stages' are C arrays. */
static size_t _peLocationArrayBytes(const void* array)
{
    size_t bytes = peAssetBindingBytes(array);
    if (bytes != 0) {
        return bytes;
    }
    static const struct {
        const PeLocationArrayExtent* list;
        const int*                   count;
    } lists[] = {
        { gPeLocationArrayExtents, &gPeLocationArrayExtentCount },
        { gMapShelterLocationExtents, &gMapShelterLocationExtentCount },
        { gMapNeoArkLocationExtents, &gMapNeoArkLocationExtentCount },
    };
    for (int list = 0; list < ARRAY_SIZE(lists); list++) {
        for (int i = 0; i < *lists[list].count; i++) {
            if (lists[list].list[i].array == array) {
                return lists[list].list[i].bytes;
            }
        }
    }
    return 0;
}

static const PeLocationTables sGameTables = {
    .areaCount     = { ARRAY_SIZE(gAkropolisStageAreaWarpEntries), ARRAY_SIZE(gMapDryfieldAreaWarpEntries),
                       ARRAY_SIZE(gMapDryfieldFullAreaWarpEntries), ARRAY_SIZE(gMapShelterAreaWarpEntries),
                       ARRAY_SIZE(gMapNeoArkAreaWarpEntries) },
    /* MIST_R21, the empty debug room: area start runs only its room task (src/gameplay/load_screen.c). */
    .debugArea     = { GAME_AREA_MIST_R21, 0, 0, 0, 0 },
    .warpArray     = _peLocationWarpArray,
    .roomArray     = _peLocationRoomArray,
    .arrayBytes    = _peLocationArrayBytes,
    .warpEntrySize = sizeof(DirectionWarpEntry),
    .roomSize      = sizeof(WorldCollisionRoomResources),
};

const PeLocationTables* peLocationGameTables(void)
{
    return &sGameTables;
}

int peLocationAcceptDev(const char* source, unsigned stage, unsigned area, unsigned room, unsigned warp)
{
    PeLocationReport report;
    char             reason[256];

    switch (peLocationCheck(&sGameTables, stage, area, room, warp, &report)) {
        case PE_LOCATION_OK:
        case PE_LOCATION_DEBUG_ROOM:
            return 1;
        default:
            peLocationDescribe(&report, stage, area, room, warp, reason, sizeof(reason));
            fprintf(stderr, "[location] %s rejected: %s\n", source, reason);
            return 0;
    }
}

int peLocationAuditTriggers(void)
{
    enum { MAX_TRIGGERS_PER_ROOM = 256 };
    int problems = 0, warps = 0;

    for (unsigned stage = 1; stage <= PE_LOCATION_STAGE_COUNT; stage++) {
        for (unsigned area = 1; area <= sGameTables.areaCount[stage - 1]; area++) {
            PeLocationReport here;
            char             reason[256];
            PeLocationStatus status = peLocationCheck(&sGameTables, stage, area, 1, 1, &here);
            if (status == PE_LOCATION_NO_AREA) {
                continue;
            }
            if (status != PE_LOCATION_OK) {
                peLocationDescribe(&here, stage, area, 1, 1, reason, sizeof(reason));
                fprintf(stderr, "[location-audit] %s\n", reason);
                problems++;
                continue;
            }
            fprintf(stderr, "[location-table] stage %u area %u rooms %u warps %u\n", stage, area, here.roomCount, here.warpCount);
            const WorldCollisionRoomResources* rooms = _peLocationRoomArray(stage, area);
            for (unsigned room = 1; rooms && room <= here.roomCount; room++) {
                const WorldCollisionTrigger* trigger = rooms[room - 1].actionTriggers;
                for (int i = 0; trigger && i < MAX_TRIGGERS_PER_ROOM; i++, trigger++) {
                    if ((trigger->control & WORLD_COLLISION_TRIGGER_ACTION_MASK) == WORLD_COLLISION_TRIGGER_ACTION_WARP) {
                        unsigned         departure = trigger->parameter1 >> 4, arrival = trigger->parameter1 & 0xF;
                        PeLocationReport there;
                        warps++;
                        if (departure < 1 || departure > here.warpCount) {
                            fprintf(stderr, "[location-audit] stage %u area %u room %u trigger %d: departure endpoint %u, area has 1-%u\n",
                                    stage, area, room, i, departure, here.warpCount);
                            problems++;
                        }
                        if (peLocationCheck(&sGameTables, stage, trigger->parameter0, 1, arrival, &there) > PE_LOCATION_DEBUG_ROOM) {
                            peLocationDescribe(&there, stage, trigger->parameter0, 1, arrival, reason, sizeof(reason));
                            fprintf(stderr, "[location-audit] stage %u area %u room %u trigger %d leads to %s\n", stage, area, room, i, reason);
                            problems++;
                        }
                    }
                    if (trigger->flags & WORLD_COLLISION_TRIGGER_LAST) {
                        break;
                    }
                }
            }
        }
    }
    fprintf(stderr, "[location-audit] %d warp triggers checked, %d problems\n", warps, problems);
    return problems;
}
