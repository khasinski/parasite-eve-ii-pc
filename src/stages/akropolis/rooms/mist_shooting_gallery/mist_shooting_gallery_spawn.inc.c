/// Spawns, places and counts one scripted target mount for the active course.
static Enemy* _mistShootingGallerySpawnTarget(Task* controller, const _MistShootingGallerySpawn* spawn)
{
    enum {
        SPAWN_HIGH_HALF_SHIFT = 16,
        TARGET_CLUT_ROW       = 2,
    };
    MistShootingGalleryWork* work;
    Enemy*                   enemy;
    TmdObject*               model;
    GfxCoord*                rootCoord;

    work  = controller->work;
    enemy = enemySpawnFromTable(gShootingGalleryTargetsTaskDescriptors, SHOOTING_GALLERY_TARGETS_TASK_MOUNT, spawn->spawnArgLo | (spawn->spawnArgHi << SPAWN_HIGH_HALF_SHIFT), NULL);
    if (enemy != NULL) {
        // Reparent must detach from the actual previous owner. Overwriting parent
        // first makes an unparented mount clear the controller's existing children.
        taskReparent(controller, enemy->task);
        model                    = enemy->task->extra.tmd;
        model->texturePageOffset = 0;
        model->clutRowOffset     = TARGET_CLUT_ROW;
        // Both primitive-buffer halves must use the gallery palette.
        tmdBuildBufferHalf(model);
        tmdBuildBufferHalf(model);
        rootCoord             = enemy->task->extra.tmd->coords;
        rootCoord->coord.t[0] = spawn->x;
        rootCoord->coord.t[1] = spawn->y;
        rootCoord->coord.t[2] = spawn->z;
        enemy->workType       = ENEMY_WORK_ACTOR;
        work->liveTargets++;
    }
    return enemy;
}
