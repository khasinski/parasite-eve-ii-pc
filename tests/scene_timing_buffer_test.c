#include "main/fs.h"
#include "main/stream_types.h"
#include "main/session.h"
#include "main/mc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
CdCmdQueue gCdCmdQueue;
static GameSession session;
GameSession* gGameSession=&session;
McSaveData gMcSaveData[MEMORY_CARD_SAVE_COUNT];
u8 *gFsActorBuffer0Base,*gFsActorBuffer1Base,*gFsActorBuffer2Base;
s32 gAkropolisStageMovieWorkspaceBytesByArea[21],gMapDryfieldMovieWorkspaceBytesByArea[32],gMapNeoArkMovieWorkspaceBytesByArea[21];
static u32 allocation[4][512];
static unsigned allocations;
static u16 vlcAllocations[2][STREAM_VLC_TABLE_BYTES/sizeof(u16)];
static unsigned vlcAllocationCount,vlcBuilds;
static int failAllocation;
static unsigned movieQueries;
void* memMalloc(size_t size,bool aux)
{
    CHECK(aux);
    if(failAllocation)return NULL;
    void* result;
    if(size==STREAM_VLC_TABLE_BYTES) {
        CHECK(vlcAllocationCount<2);
        result=vlcAllocations[vlcAllocationCount++];
    } else {
        CHECK(size==sizeof allocation[0] && allocations<4);
        result=allocation[allocations++];
    }
    memset(result,0xa5,size);
    return result;
}
s16 streamFindMovieSlot(const GameLocationKey* key,s32 sub,s32 required) {(void)key;(void)sub;(void)required;++movieQueries;return -1;}
void DecDCTvlcBuild(u_short* table) {++vlcBuilds;memset(table,0x6b,STREAM_VLC_TABLE_BYTES);}
int main(void)
{
    static u32 actor[3][STREAM_VLC_TABLE_BYTES/sizeof(u32)+512];StreamSlot stream={0};
    gFsActorBuffer0Base=(u8*)actor[0];gFsActorBuffer1Base=(u8*)actor[1];gFsActorBuffer2Base=(u8*)actor[2];session.location.stage=1;
    for(int kind=1;kind<=4;kind++)for(int shared=0;shared<2;shared++) {
        memset(&gCdCmdQueue,0,sizeof gCdCmdQueue);memset(actor,0xa5,sizeof actor);allocations=0;
        stream.control.scene.timingBufferKind=kind;stream.data.scene.timingBufferBytes=sizeof allocation[0];
        stream.data.scene.vlcBufferKind=shared && kind>=2?kind-1:0;
        gCdCmdQueue.sceneBuffersNeeded=1;gCdCmdQueue.sceneVlcTableMode=STREAM_SCENE_VLC_IMAGE_BUFFER;gCdCmdQueue.sceneStream=&stream;
        cdCmdReservePlaybackBuffers();
        CHECK(gCdCmdQueue.timingCursor==gCdCmdQueue.timingBuffer);
        CHECK(gCdCmdQueue.timingCursor[0]==CD_COMMAND_TIMING_END);
        CHECK(gCdCmdQueue.timingCursor[1]==0xa5a5a5a5u);
        if(shared && kind>=2)CHECK(actor[kind-2][0]==0xa5a5a5a5u); /* VLC prefix untouched. */
        /* Decoder payload replaces the bootstrap sentinel normally. */
        u32 payload[]={120,240,0};memcpy(gCdCmdQueue.timingBuffer,payload,sizeof payload);
        CHECK(gCdCmdQueue.timingCursor[0]==120 && gCdCmdQueue.timingCursor[2]==0);
    }
    memset(&gCdCmdQueue,0,sizeof gCdCmdQueue);allocations=0;movieQueries=0;
    stream.control.scene.timingBufferKind=STREAM_TIMING_BUFFER_ALLOCATE;
    gCdCmdQueue.sceneStream=&stream;gCdCmdQueue.sceneBuffersNeeded=1;
    gCdCmdQueue.sceneVlcTableMode=STREAM_SCENE_VLC_IMAGE_BUFFER;
    gCdCmdQueue.decodeBufferBytes=sizeof allocation[0];
    CHECK(cdCmdEnsurePlaybackBuffers() && allocations==2 && movieQueries==0);
    CHECK((void*)gCdCmdQueue.decodeBuffer!=(void*)gCdCmdQueue.timingBuffer);
    gCdCmdQueue.timingBuffer[0]=123;
    CHECK(cdCmdEnsurePlaybackBuffers() && allocations==2 && gCdCmdQueue.timingBuffer[0]==123);
    gCdCmdQueue.decodeBuffer=NULL;
    CHECK(!cdCmdEnsurePlaybackBuffers() && allocations==2); /* Partial storage is not silently reset. */
    gCdCmdQueue.timingBuffer=NULL;failAllocation=1;
    gCdCmdQueue.sceneVlcTableMode=STREAM_SCENE_VLC_RESERVED_TABLE;
    stream.data.scene.vlcBufferKind=STREAM_VLC_BUFFER_ALLOCATE;
    CHECK(!cdCmdEnsurePlaybackBuffers());
    /* Re-entering a camera allocates a new table even when the preceding
     * camera's table was built. Poisoned fresh storage must be initialized. */
    failAllocation=0;
    memset(&gCdCmdQueue,0,sizeof gCdCmdQueue);
    stream.control.scene.timingBufferKind=STREAM_TIMING_BUFFER_NONE;
    stream.data.scene.vlcBufferKind=STREAM_VLC_BUFFER_ALLOCATE;
    gCdCmdQueue.sceneStream=&stream;
    gCdCmdQueue.sceneBuffersNeeded=1;
    gCdCmdQueue.sceneVlcTableMode=STREAM_SCENE_VLC_RESERVED_TABLE;
    gCdCmdQueue.vlcTableBuilt=1;
    cdCmdReservePlaybackBuffers();
    CHECK(vlcAllocationCount==1 && vlcBuilds==1 && gCdCmdQueue.vlcTable[0]==0x6b6b);
    u16* firstTable=gCdCmdQueue.vlcTable;
    cdCmdReservePlaybackBuffers();
    CHECK(vlcAllocationCount==2 && vlcBuilds==2 && gCdCmdQueue.vlcTable!=firstTable);
    CHECK(gCdCmdQueue.vlcTable[0]==0x6b6b);
    /* A prefetched compressed image outlives the transient view allocations.
     * Re-reserving a camera must not point its retained header at empty bytes. */
    memset(&gCdCmdQueue,0,sizeof gCdCmdQueue);
    allocations=0;
    gCdCmdQueue.sceneStream=&stream;
    gCdCmdQueue.sceneBuffersNeeded=1;
    gCdCmdQueue.sceneVlcTableMode=STREAM_SCENE_VLC_IMAGE_BUFFER;
    gCdCmdQueue.decodeBufferBytes=sizeof allocation[0];
    cdCmdReservePlaybackBuffers();
    u8* prefetched=gCdCmdQueue.decodeBuffer;
    CHECK(prefetched!=NULL);
    memset(prefetched,0x3c,sizeof allocation[0]);
    gCdCmdQueue.sceneImageHeaders[0].sectorCount=1;
    gCdCmdQueue.sceneImageHeaders[0].viewId=16;
    cdCmdReservePlaybackBuffers();
    CHECK(allocations==2); /* Keep the game's original heap reservations. */
    CHECK(gCdCmdQueue.decodeBuffer==prefetched);
    for(unsigned i=0;i<sizeof allocation[0];++i)CHECK(prefetched[i]==0x3c);
    puts("Scene timing: poisoned allocated/borrowed buffers start terminated; VLC prefix and payload preserved");return 0;
}
