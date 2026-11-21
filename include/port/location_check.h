#ifndef PORT_LOCATION_CHECK_H
#define PORT_LOCATION_CHECK_H

#include <stddef.h>

/* Validation of a requested game location (stage, area, room, warp) against the tables the game indexes with it.
 *
 * Room start reads `warpEntries[stage - 1][area - 1][warp - 1]` and `areaRooms[stage - 1][area - 1][room - 1]`
 * with no bounds check, as the original does: every location the game itself produces is in range. Development
 * switches (PE2_START_LOCATION, PE2_WARP) take arbitrary numbers, so they are checked here first. The directories
 * store bare pointers; each borrowed array's extent comes from `arrayBytes`. */

enum { PE_LOCATION_STAGE_COUNT = 5 };

typedef enum {
    PE_LOCATION_OK = 0,
    PE_LOCATION_DEBUG_ROOM,     /* the stage's debug room past its directory (MIST_R21): only its room task runs */
    PE_LOCATION_BAD_STAGE,      /* stage outside 1..5 */
    PE_LOCATION_BAD_AREA,       /* area outside the stage's directory */
    PE_LOCATION_NO_AREA,        /* the directory has no warp table for this area */
    PE_LOCATION_UNKNOWN_EXTENT, /* a borrowed array whose size the port does not know */
    PE_LOCATION_BAD_ROOM,       /* room (roomState) outside the area's collision-room array */
    PE_LOCATION_BAD_WARP,       /* warp outside the area's warp-endpoint table */
} PeLocationStatus;

typedef struct {
    unsigned areaCount[PE_LOCATION_STAGE_COUNT]; /* directory extents (number of area slots) */
    unsigned debugArea[PE_LOCATION_STAGE_COUNT]; /* an accepted area past the directory, or 0 */
    const void* (*warpArray)(unsigned stage, unsigned area); /* the area's warp-endpoint array, or NULL */
    const void* (*roomArray)(unsigned stage, unsigned area); /* the area's collision-room array, or NULL */
    size_t (*arrayBytes)(const void* array);                 /* byte size of a borrowed array, 0 when unknown */
    size_t warpEntrySize;                                    /* sizeof(DirectionWarpEntry) */
    size_t roomSize;                                         /* sizeof(WorldCollisionRoomResources) */
} PeLocationTables;

typedef struct {
    PeLocationStatus status;
    unsigned         areaCount; /* directory extent of the requested stage (0 for a bad stage) */
    unsigned         roomCount; /* rooms in the area (1 when the area has no collision rooms) */
    unsigned         warpCount; /* warp endpoints in the area */
} PeLocationReport;

/* Checks a 1-based location. Returns the status, also stored in `report` (which may be NULL). */
PeLocationStatus peLocationCheck(const PeLocationTables* tables, unsigned stage, unsigned area, unsigned room,
                                 unsigned warp, PeLocationReport* report);
/* Writes a one-line explanation of a checked location (no trailing newline); returns snprintf's result. */
int peLocationDescribe(const PeLocationReport* report, unsigned stage, unsigned area, unsigned room, unsigned warp,
                       char* buffer, size_t size);

/* Byte extent of one C-defined array an area directory borrows (src/port/location_extents.c). */
typedef struct {
    const void* array;
    size_t      bytes;
} PeLocationArrayExtent;
extern const PeLocationArrayExtent gPeLocationArrayExtents[];
extern const int                   gPeLocationArrayExtentCount;

/* The game's own directories (src/port/location_tables.c). */
const PeLocationTables* peLocationGameTables(void);
/* Validates a development location request; logs the reason to stderr and returns 0 when it is rejected. */
int peLocationAcceptDev(const char* source, unsigned stage, unsigned area, unsigned room, unsigned warp);
/* PE2_LOCATION_AUDIT=1: lists every area's room and warp counts ("[location-table] stage S area A rooms R warps W"),
 * checks every warp trigger of every room (both endpoints) and prints findings to stderr; returns the number of
 * problems. */
int peLocationAuditTriggers(void);

#endif
