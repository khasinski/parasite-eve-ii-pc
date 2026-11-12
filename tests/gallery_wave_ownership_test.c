#ifdef NDEBUG
#undef NDEBUG
#endif
#include "port/gpu_len.h"
#include "port/asset_bind.h"
/* This fixture supplies its spawn record directly; it never loads a room.
 * Retained COFF asset registrations would root unrelated course callbacks. */
#undef PE_ASSET_BIND_AUX
#define PE_ASSET_BIND_AUX(key, array, auxArray)
#include "../src/stages/akropolis/rooms/mist_shooting_gallery/mist_shooting_gallery_courses.c"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

DisplayState gDisplayState;
TaskDesc gShootingGalleryTargetsTaskDescriptors[SHOOTING_GALLERY_TARGETS_TASK_COUNT];
static Task mounts[3];
static Enemy enemies[3];
static TmdObject models[3];
static GfxCoord coords[3];
static unsigned spawned;
Enemy* enemySpawnFromTable(TaskDesc* table, s32 index, s32 arg, Enemy* parent)
{
    assert(table == gShootingGalleryTargetsTaskDescriptors);
    assert(index == SHOOTING_GALLERY_TARGETS_TASK_MOUNT && parent == NULL);
    unsigned i = spawned++;
    assert(i < 3);
    mounts[i].nextSibling = &mounts[i];
    mounts[i].spawnArg1.value = arg;
    mounts[i].extra.tmd = &models[i];
    models[i].coords = &coords[i];
    enemies[i].task = &mounts[i];
    return &enemies[i];
}
void tmdBuildBufferHalf(TmdObject* model) { (void)model; }
void memFree(void* ptr) { (void)ptr; abort(); }
void modelObjectUnlinkTmd(TmdListNode* node) { (void)node; abort(); }
void modelObjectFreeTmd(TmdObject* model) { (void)model; abort(); }
void modelObjectUnlinkCoordBody(TmdListNode* node) { (void)node; abort(); }
void modelObjectFreeCoordBody(ModelObjectCoordBody* body) { (void)body; abort(); }
int main(void)
{
    MistShootingGalleryWork work = {0};
    Task controller = {0}; controller.work = &work;
    const _MistShootingGallerySpawn spawn = {.x=7500,.z=4500};
    for (unsigned count=1;count<=3;count++) {
        assert(_mistShootingGallerySpawnTarget(&controller,&spawn));
        assert(work.liveTargets == count);
        Task* p = controller.firstChild;
        assert(p == &mounts[0]);
        for (unsigned i=0;i<count;i++) {
            assert(p == &mounts[i] && p->parent == &controller);
            assert(p->extra.tmd->coords->coord.t[0] == 7500);
            p=p->nextSibling;
        }
        assert(p == controller.firstChild);
    }
    puts("Gallery waves preserve every mount in the controller child ring");
}
