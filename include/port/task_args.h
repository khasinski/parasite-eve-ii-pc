#ifndef PORT_TASK_ARGS_H
#define PORT_TASK_ARGS_H

/* Host replacement for GCC's transparent_union call convention.
 *
 * On the PlayStation `TaskSpawnArg` is a transparent union of 32-bit members, so callers
 * pass an integer or a pointer directly. On the host its members differ in size (a pointer
 * is 8 bytes), which clang does not accept as transparent. The spawn helpers are therefore
 * called through macros that wrap each argument with the member matching its type. The
 * functions themselves are declared and defined with a parenthesised name, `(f)(...)`,
 * which keeps the macro of the same name from expanding there. */

#include "decomp/types.h"

#include <stdint.h>
#include <stdio.h>

static inline TaskSpawnArg TaskArg_Same(TaskSpawnArg arg) { return arg; }
static inline TaskSpawnArg TaskArg_S32(s32 value) {
    TaskSpawnArg arg = {0};
    arg.value = value;
    return arg;
}
static inline TaskSpawnArg TaskArg_U32(u32 value) {
    TaskSpawnArg arg = {0};
    arg.unsignedValue = value;
    return arg;
}
/* 64-bit integers keep the PS1 word: a value outside 32 bits (most likely a host
 * pointer cast to an integer) would be truncated, so it is reported once. */
static inline void TaskArg_ReportTruncation(unsigned long long value) {
    static int reported;
    if (!reported) {
        reported = 1;
        fprintf(stderr, "[task] argument 0x%llx does not fit the 32-bit task word\n", value);
    }
}
static inline TaskSpawnArg TaskArg_S64(long long value) {
    if (value != (long long)(s32)value && value != (long long)(u32)value) {
        TaskArg_ReportTruncation((unsigned long long)value);
    }
    return TaskArg_S32((s32)value);
}
static inline TaskSpawnArg TaskArg_U64(unsigned long long value) {
    if (value > 0xFFFFFFFFull) {
        TaskArg_ReportTruncation(value);
    }
    return TaskArg_U32((u32)value);
}
static inline TaskSpawnArg TaskArg_Ptr(const void* pointer) {
    TaskSpawnArg arg = {0};
    arg.pointer = (void*)pointer;
    return arg;
}

/* The whole argument word: a pointer on the host keeps all 64 bits, scalars are zero-extended.
 * Use it where a callback tells a pointer from a small selector by magnitude. */
static inline unsigned long long TaskArg_Word(TaskSpawnArg arg) { return (unsigned long long)(uintptr_t)arg.pointer; }

#define TASK_ARG(x)                                                                              \
    _Generic((x),                                                                                \
        TaskSpawnArg: TaskArg_Same,                                                              \
        signed char: TaskArg_S32, short: TaskArg_S32, int: TaskArg_S32, long: TaskArg_S64,       \
        long long: TaskArg_S64,                                                                  \
        unsigned char: TaskArg_U32, unsigned short: TaskArg_U32, unsigned int: TaskArg_U32,      \
        unsigned long: TaskArg_U64, unsigned long long: TaskArg_U64,                             \
        default: TaskArg_Ptr)(x)

#define effectSpawn(effectId, parentCoord, spawnArg, offset) \
    (effectSpawn)((effectId), (parentCoord), TASK_ARG(spawnArg), (offset))
#define uiSpawnObject(descriptor, contentArg, controlMode, openingDelayTicks, parent) \
    (uiSpawnObject)((descriptor), TASK_ARG(contentArg), (controlMode), (openingDelayTicks), (parent))
#define taskSpawnFromTable(table, index, spawnArg1, spawnArg2) \
    (taskSpawnFromTable)((table), (index), TASK_ARG(spawnArg1), TASK_ARG(spawnArg2))
#define taskSpawn(bank, selector, spawnArg1, spawnArg2) \
    (taskSpawn)((bank), TASK_ARG(selector), TASK_ARG(spawnArg1), TASK_ARG(spawnArg2))
#define taskSpawnFromTableOnDefaultList(table, index, spawnArg1, spawnArg2) \
    (taskSpawnFromTableOnDefaultList)((table), (index), TASK_ARG(spawnArg1), TASK_ARG(spawnArg2))
#define taskSpawnOnDefaultList(bank, selector, spawnArg1, spawnArg2) \
    (taskSpawnOnDefaultList)((bank), TASK_ARG(selector), TASK_ARG(spawnArg1), TASK_ARG(spawnArg2))
#define displaySpawnTask(bank, selector, spawnArg1, spawnArg2) \
    (displaySpawnTask)((bank), TASK_ARG(selector), TASK_ARG(spawnArg1), TASK_ARG(spawnArg2))
#define displaySpawnTaskFromTable(table, index, spawnArg1, spawnArg2) \
    (displaySpawnTaskFromTable)((table), (index), TASK_ARG(spawnArg1), TASK_ARG(spawnArg2))
#define displayQueueModeTask(descriptor, spawnArg1, spawnArg2, entryMode) \
    (displayQueueModeTask)((descriptor), (spawnArg1), TASK_ARG(spawnArg2), (entryMode))
#define _taskSpawnFromDesc(desc, spawnArg1, spawnArg2, listHead) \
    (_taskSpawnFromDesc)((desc), TASK_ARG(spawnArg1), TASK_ARG(spawnArg2), (listHead))
#define _enemySpawn(bank, selector, spawnArg, parent) \
    (_enemySpawn)((bank), TASK_ARG(selector), TASK_ARG(spawnArg), (parent))

#endif
