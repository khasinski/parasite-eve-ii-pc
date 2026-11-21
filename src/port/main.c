#include "port/disc_assets.h"
#include "port/asset_bind.h"
#include "port/host_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <psyz.h>
#include <psyz/cd.h>
#include <psyz/video.h>
#include <libgpu.h>
#include <libetc.h>
#include "gamemain.h"

#include "port/crash_report.h"
#include "port/pad_trace.h"
#include "main/gamemain.h"
#include "main/mc.h"
#include "main/display.h"

#ifndef PE2_VERSION
#define PE2_VERSION "unknown"
#endif

/* Crash report (src/port/crash_report.c): the game state it records. The task callback is the last one the task
 * walk dispatched, since a call through a NULL function pointer leaves no frame of its own. */
extern void* gHostCurrentTaskCallback;
extern int pePadTickCount;
static void installCrashReporter(void)
{
    peCrashInstall(PE2_VERSION);
    GameLocationKey* location = &gMcSaveData[MEMORY_CARD_SAVE_LIVE].state.location;
    peCrashWatch("stage", &location->stage, 1, PE_CRASH_WATCH_UNSIGNED);
    peCrashWatch("area", &location->area, 1, PE_CRASH_WATCH_UNSIGNED);
    peCrashWatch("view", &location->view, 1, PE_CRASH_WATCH_UNSIGNED);
    peCrashWatch("room state", &location->roomState, 1, PE_CRASH_WATCH_UNSIGNED);
    peCrashWatch("warp", &location->warp, 1, PE_CRASH_WATCH_UNSIGNED);
    peCrashWatch("variant", &location->variant, 1, PE_CRASH_WATCH_UNSIGNED);
    peCrashWatch("disc (0 unknown)", &gGameMainPersistentState.discNumber, 1, PE_CRASH_WATCH_UNSIGNED);
    peCrashWatch("pad tick", &pePadTickCount, sizeof pePadTickCount, PE_CRASH_WATCH_SIGNED);
    peCrashWatch("game frame", &gDisplayState.frameCount, sizeof gDisplayState.frameCount, PE_CRASH_WATCH_SIGNED);
    peCrashWatch("vsync count", &gDisplayState.vsyncCount, sizeof gDisplayState.vsyncCount, PE_CRASH_WATCH_SIGNED);
    peCrashWatch("last task callback", &gHostCurrentTaskCallback, sizeof(void*), PE_CRASH_WATCH_CODE);
}

/* Port entry. Disc mounting goes here once the runtime loader exists; then the
 * original start-up path runs on the host stack (no SetSp). */
int main(int argc, char** argv) {
    /* Input traces replay only on emulated time: set before PsyZ/SDL read their clocks. */
    int clocks = pePadTraceDefaultClocks();
    /* Reports go to standard error until peHostStartup knows the preferences folder. */
    installCrashReporter();
    /* Disc image: first argument, or PE2_DISC. Game data is read from it at run time. */
    const char* disc = NULL;
    int startup = peHostStartup(argc, argv, &disc);
    if (startup <= 0) return startup < 0 ? 2 : 0;
    if (clocks)
        fprintf(stderr, "[pad] input trace: deterministic clocks (%s%s%s); set PSYZ_AUDIO_CLOCK/PSYZ_RASTER_CLOCK=host to opt out\n",
                clocks & PE_PAD_CLOCK_AUDIO ? "PSYZ_AUDIO_CLOCK=video" : "", clocks == (PE_PAD_CLOCK_AUDIO | PE_PAD_CLOCK_RASTER) ? ", " : "",
                clocks & PE_PAD_CLOCK_RASTER ? "PSYZ_RASTER_CLOCK=virtual" : "");
    if (Psyz_CdSetDiskPath(disc) != 0) {
        peHostReportError("Cannot mount the disc image \"%s\". Check that the file exists and that it is a CUE sheet or BIN "
                          "image of the North American Parasite Eve II (disc 1). Choose another image with --host-menu.", disc);
        return 2;
    }
    Psyz_SetTitle("Parasite Eve II");
    /* Game tables the original source embedded at build time are filled from the disc here. */
    {
        extern int peAssetsDump(const char*);
        struct PeDisc* assets = peDiscOpen(disc);
        if (!assets) {
            peHostReportError("Cannot read the game files from \"%s\": %s.", disc, peDiscError());
            return 2;
        }
        int loaded = peAssetsLoad(assets);
        if (loaded <= 0) {
            peHostReportError("Cannot load the game tables from \"%s\"; the image may be damaged.", disc);
            peDiscClose(assets);
            return 2;
        }
        fprintf(stderr, "[assets] %d arrays loaded from the disc\n", loaded);
        if (getenv("PE2_DISC2_ASSETS")) { /* development: load disc 2's arrays now, to compare the dump */
            struct PeDisc* second = peDiscOpen(getenv("PE2_DISC2_ASSETS"));
            if (second) { fprintf(stderr, "[assets] %d arrays loaded from disc 2\n", peAssetsLoad(second)); peDiscClose(second); }
        }
        if (getenv("PE2_DUMP_ASSETS")) peAssetsDump(getenv("PE2_DUMP_ASSETS"));
        peDiscClose(assets);
        if (getenv("PE2_LOCATION_AUDIT")) { /* development: check every warp trigger against the warp tables, then exit */
            extern int peLocationAuditTriggers(void);
            return peLocationAuditTriggers() ? 1 : 0;
        }
    }
    if (getenv("PE2_DEBUG_PORT")) { /* development: PsyZ HTTP debug server on 127.0.0.1 (/screenshot, /vram, /input) */
        extern int Psyz_DebugServer(int);
        int debugPort = atoi(getenv("PE2_DEBUG_PORT"));
        if (debugPort <= 0 || debugPort > 65535) {
            fprintf(stderr, "[debug] ignoring invalid PE2_DEBUG_PORT: %s\n", getenv("PE2_DEBUG_PORT"));
        } else {
            fprintf(stderr, "[debug] server on port %d\n", Psyz_DebugServer(debugPort));
        }
    }
    ResetGraph(0);
    gameMainRun();
    return 0;
}
