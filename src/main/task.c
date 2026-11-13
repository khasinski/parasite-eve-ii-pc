#include <stdio.h>
#include "task.h"

#include "types.h"

#include "main/display.h"
#include "main/display_types.h"
#include "main/mem.h"
#include "main/session.h"
#include "main/session_types.h"
#include "main/task_types.h"
#include "main/tmd.h"

#include "gameplay/display.h"
#include "gameplay/model_objects.h"

/// Borrowed head of the selected execution list for spawning and tail unlinking.
///
/// The selected head is an initialized bare `TaskNode`, not an embedded task
/// node. It must remain alive while selected; this pointer owns neither the
/// head nor its tasks. NULL is the pre-initialization value, not an empty list.
///
/// Initialization selects its head. Unfiltered walks select their head on entry
/// without restoring the previous selection; filtered walks restore it,
/// including on an early stop. Callers making temporary switches save and
/// restore it explicitly. The default frame walk selects `gTaskDefaultList`
/// before invoking callbacks; ordinary spawns use the current selection.
static TaskNode* _gTaskActiveList;

/* Define BSS before API headers to preserve first-declaration order. */

TaskNode gTaskDefaultList;

/// BSS bytes with no established use; their original type and role are unproven.
u8 D_800716E8[8];

#include "main/task.h"

#ifdef __psyz
void* gHostCurrentTaskCallback; /* last dispatched task callback, for the crash report */
#endif


/// Mask selecting the low-byte body kind from a task descriptor's flags halfword.
///
/// Excludes the upper-byte options from attachment dispatch and `Task::bodyKind`.
/// Selectors are 0 (no body), 1 (TMD model) and 2 (coordinate body). Zero permits
/// a bodyless spawn; every nonzero selector requires successful attachment, so
/// other selector values fail to spawn.
enum { TASK_DESC_BODY_KIND_MASK = 0xFF };

/// Completed body teardown awaiting execution-list collection.
///
/// Stored in `Task::bodyKind` once its owned body has been freed, or when
/// teardown has no body to release. `TASK_BODY_NONE` instead denotes a live
/// task without a body. Teardown leaves `Task::extra` unchanged; its pointer
/// must no longer be dereferenced.
///
/// Walkers unlink and free marked tasks after callback dispatch or the filter
/// decision, unless a stop request ends the walk first. Immediate teardown
/// unlinks and frees the task without setting this marker.
enum { TASK_BODY_RELEASED = 0xFF };

/// Values used by the task stop protocol.
enum {
    TASK_STATUS_STOP_REQUESTED = 0xFF
};

/// Low byte of a signed priority selector, compared with the stored task byte.
enum { TASK_PRIORITY_MASK = 0xFF };

/// Exact stop request consumed by a walker before collection or advancement.
enum { TASK_WALK_STOP_REQUESTED = 1 };

static Task* (_taskSpawnFromDesc)(TaskDesc* desc, TaskSpawnArg spawnArg1, TaskSpawnArg spawnArg2, TaskNode* listHead);

static void _taskUnlinkFromSelectedList(Task* task);

static void _taskFree(Task* task);
static inline void _taskDetachForReparent(Task* task);

/* Active callback dispatches borrow task pointers. Retirement updates every
 * nested dispatch before storage can be freed or reused. */
typedef struct TaskDispatchFrame {
    struct TaskDispatchFrame* previous;
    Task* cursor;
    Task* successor;
} TaskDispatchFrame;
static TaskDispatchFrame* _gTaskDispatchFrame;

static void _taskNotifyRetirement(Task* task)
{
    TaskDispatchFrame* frame;
    for (frame = _gTaskDispatchFrame; frame != NULL; frame = frame->previous) {
        if (frame->cursor == task) {
            frame->cursor = NULL;
            frame->successor = task->node.next;
        } else if (frame->successor == task) {
            frame->successor = task->node.next;
        }
    }
}


bool gTaskDeferModelBufferAllocation = false;

/// Inserts a new task into its execution list, preserving ascending priority and spawn order among equals.
///
/// `listHead` must be a non-NULL bare head of a live, NULL-terminated list already
/// ordered by priority.
/// An empty head has `next == NULL` and `prev == listHead`; otherwise its `prev`
/// points to the last task's node. `task` must be live and not already linked.
/// `priority` is the task's initialized byte priority, 0..255, widened to `u32`
/// for the unsigned comparison. Insertion overwrites both of the task's links
/// and updates the successor's back link, or the head's tail link when appending.
/// The head and tasks must remain live while linked. Insertion performs no
/// allocation, callback dispatch or release, and does not select the active list.
static inline void _taskInsert(TaskNode* listHead, Task* task, u32 priority)
{
    Task*      nextTask;
    TaskNode** backlinkSlot;
    TaskNode*  predecessorNode;

    for (nextTask = listHead->next; nextTask != NULL; nextTask = nextTask->node.next) {
        if (priority < nextTask->priority) {
            break;
        }
    }

    // The insertion slot holds the predecessor, which may be the bare list head.
    if (nextTask == NULL) {
        backlinkSlot = &listHead->prev;
    } else {
        backlinkSlot = &nextTask->node.prev;
    }
    task->node.next       = (*backlinkSlot)->next;
    predecessorNode       = *backlinkSlot;
    predecessorNode->next = task;
    task->node.prev       = *backlinkSlot;
    *backlinkSlot         = &task->node;
}

/// Creates and inserts a task from a synchronously borrowed descriptor.
///
/// `desc` and `listHead` must be live and non-NULL; the head must already be
/// initialized and ordered by ascending priority. Only body kinds 0, 1 and 2
/// are accepted. Task allocation or required body attachment failure returns
/// NULL and leaves the execution list unchanged. Primitive-buffer allocation
/// failure alone can leave a valid model body with a NULL buffer.
///
/// Copies the callback, low-byte priority and both payload words. The task owns
/// its attached body; callback code and borrowed model geometry must outlive
/// their use. No descriptor pointer is retained and no task callback runs here.
/// Equal priorities retain spawn order, so a spawn after a walk's cursor can
/// run in that same walk. Pointer payload ownership belongs to the callback.
static Task* (_taskSpawnFromDesc)(TaskDesc* desc, TaskSpawnArg spawnArg1, TaskSpawnArg spawnArg2, TaskNode* listHead)
{
    /// Spawn-time creation flag that defers a TMD body's initial primitive buffer.
    ///
    /// Bit 1 of the signed 32-bit buffer-flags argument, added while
    /// `gTaskDeferModelBufferAllocation` is set. A nonzero creation argument
    /// skips auxiliary-heap buffer allocation but still creates the model body
    /// and its coordinates. This bit does not set `TMD_OBJECT_SKIP_AUTO_BUFFER`;
    /// recovery remains eligible unless `TASK_DESC_SKIP_AUTO_MODEL_BUFFER` also
    /// supplies bit 0. No allocation is scheduled by this flag: a later
    /// buffer-allocation pass must run.
    enum { TASK_SPAWN_DEFER_MODEL_BUFFER = 1 << 1 };

    Task*    task;
    TaskBody body;
    u16      descriptorFlags;
    s32      bufferFlags;
    u8       priority;
    s32      bodyKind;

    task = memCalloc(sizeof(*task), false);
    if (task == NULL) {
        return NULL;
    }

    descriptorFlags = desc->header.fields.flags;
    switch (descriptorFlags & TASK_DESC_BODY_KIND_MASK) {
        case TASK_BODY_TMD:
            bufferFlags = 0;
            if (descriptorFlags & TASK_DESC_SKIP_AUTO_MODEL_BUFFER) {
                // Descriptor bit 8 becomes creation bit 0, suppressing allocation and recovery.
                bufferFlags = TMD_CREATE_SKIP_AUTO_BUFFER;
            }
            if (gTaskDeferModelBufferAllocation) {
                bufferFlags |= TASK_SPAWN_DEFER_MODEL_BUFFER;
            }
            body.tmd = modelObjectAttachTmdWithBufferFlags(task, desc->data.model, bufferFlags);
            break;
        case TASK_BODY_COORD:
            body.coordBody = modelObjectAttachCoordBody(task);
            break;
        case TASK_BODY_NONE:
        default:
            body.allocation = NULL;
            break;
    }

    // A descriptor that asks for a body gets no task when the body cannot be attached.
    if ((desc->header.fields.flags & TASK_DESC_BODY_KIND_MASK) == TASK_BODY_NONE || body.allocation != NULL) {
#ifdef __psyz
        if (desc->callback == NULL) {
            fprintf(stderr, "task descriptor %p has no handler (flags=0x%x)\n", (void*)desc, (unsigned)desc->header.fields.flags);
        }
#endif
        task->callback     = desc->callback;
        priority           = desc->header.fields.priority;
        task->exitCallback = taskKill;
        task->priority     = priority;
        bodyKind           = desc->header.fields.flags & TASK_DESC_BODY_KIND_MASK;
        task->extra        = body;
        task->spawnArg1    = spawnArg1;
        task->spawnArg2    = spawnArg2;
        task->parent       = NULL;
        task->firstChild   = NULL;
        task->nextSibling  = task;
        task->bodyKind     = bodyKind;

        _taskInsert(listHead, task, priority);
    } else {
        memFree(task);
        task = NULL;
    }
    return task;
}

/// Collects a torn-down task immediately instead of leaving it to a list walker.
///
/// `task` must be non-NULL, live, allocated from the primary heap and still
/// linked in an execution list. The caller must finish child/parent teardown
/// and release its work and body first; their pointer slots need not be cleared.
/// This helper repairs execution-list links and frees only the task allocation,
/// without dispatching callbacks or setting the deferred-collection marker.
///
/// A task with no successor must belong to `gTaskDefaultList`: that head is
/// selected for collection regardless of the previous selection. The predecessor
/// may be a bare head or an embedded task node. The previous list selection is
/// restored after release; the caller must not access the freed task.
static inline void _taskCollectImmediately(Task* task)
{
    TaskNode*  savedListHead;
    Task*      nextTask;
    TaskNode** backlinkSlot;
    TaskNode*  predecessorNode;

    savedListHead    = _gTaskActiveList;
    nextTask         = task->node.next;
    _gTaskActiveList = &gTaskDefaultList;

    // Repair the successor's back link, or the selected default head's tail link.
    if (nextTask == NULL) {
        backlinkSlot = &_gTaskActiveList->prev;
    } else {
        backlinkSlot = &nextTask->node.prev;
    }
    predecessorNode       = task->node.prev;
    *backlinkSlot         = predecessorNode;
    predecessorNode->next = task->node.next;
    _taskFree(task);
    _gTaskActiveList = savedListHead;
}

/// Stops frame updates and repeat exit dispatch for body teardown in the current call.
///
/// `task` must be non-NULL and remain allocated through the caller's teardown.
/// Both handlers become `taskNoopCallback`, and the signed `killCountdown`
/// is initialized to one and decremented to zero within this call.
/// The caller releases the body and marks the task for execution-list collection;
/// coordinate bodies are unlinked from their refresh list before this helper.
/// The task allocation remains live on return.
static inline void _taskStopForInlineBodyRelease(Task* task)
{
    /// Initial countdown for non-model body teardown completed within the same call.
    ///
    /// Stored in the signed 16-bit `Task::killCountdown`, then decremented once
    /// after both callbacks become inert. The resulting zero permits coordinate
    /// body release or bodyless-task marking before `taskKill` returns; it counts
    /// that synchronous decrement rather than future scheduler dispatches.
    enum { TASK_SYNCHRONOUS_BODY_RELEASE_COUNTDOWN = 1 };

    task->killCountdown = TASK_SYNCHRONOUS_BODY_RELEASE_COUNTDOWN;
    task->callback      = taskNoopCallback;
    task->exitCallback  = taskNoopCallback;
    task->killCountdown--;
}

/// Releases a task's body synchronously and marks the task for walker collection.
///
/// `task` must be non-NULL, live and not already released. The caller finishes
/// child/parent teardown and work cleanup before making the task collectible.
/// An owned TMD body must be on the live model list; it is unlinked here and
/// freed with its primitive buffer. A coordinate body must already be unlinked
/// from its refresh list before this call, which frees only its allocation.
/// Other body kinds release no body allocation.
///
/// Both handlers become inert and the signed halfword countdown is initialized
/// to one and decremented once. At zero, the body is released and `bodyKind`
/// becomes `TASK_BODY_RELEASED`; a nonzero result leaves it unchanged. The task
/// allocation and its execution links remain live for the walker to collect.
/// `extra` retains its released pointer and must no longer be dereferenced.
static inline void _taskReleaseBodySynchronously(Task* task)
{
    TmdObject* model;

    _taskStopForInlineBodyRelease(task);
    if (task->killCountdown == 0) {
        switch (task->bodyKind) {
            case TASK_BODY_TMD:
                model = task->extra.tmd;
                modelObjectUnlinkTmd(&model->link);
                modelObjectFreeTmd(model);
                break;
            case TASK_BODY_COORD:
                modelObjectFreeCoordBody(task->extra.coordBody);
                break;
        }
        task->bodyKind = TASK_BODY_RELEASED;
    }
}

void taskKill(Task* task)
{
    /// Countdown callback ticks before a stopped TMD model is released.
    ///
    /// Normal teardown disables active drawing and retains the linked model
    /// and its owned storage for this many countdown callback invocations.
    /// Walks that skip the task do not advance the delay; immediate teardown
    /// bypasses it.
    enum { TASK_MODEL_RELEASE_DELAY_TICKS = 2 };

    s32 bodyKind;
    s32 immediateBodyKind;

    taskCallChildExits(task);
    _taskDetachForReparent(task);

    if (task->work != NULL) {
        memFree(task->work);
    }

    if (gDisplayState.immediateTaskFree == 0) {
        bodyKind = task->bodyKind;
        switch (bodyKind) {
            case TASK_BODY_TMD:
                // Suppress active model drawing during the deferred release window.
                task->extra.tmd->flags |= TMD_OBJECT_SKIP_ACTIVE_DRAW;
                task->killCountdown     = TASK_MODEL_RELEASE_DELAY_TICKS;
                task->callback          = taskCountdownCallback;
                task->state             = 0;
                task->exitCallback      = taskNoopCallback;
                break;
            case TASK_BODY_COORD:
                // Coordinate bodies leave their refresh list before synchronous release.
                modelObjectUnlinkCoordBody(&task->extra.coordBody->link);
                _taskReleaseBodySynchronously(task);
                break;
            case TASK_BODY_NONE:
            default:
                _taskReleaseBodySynchronously(task);
                break;
        }
        return;
    }

    // Immediate teardown bypasses the draw delay and walker collection.
    immediateBodyKind = task->bodyKind;
    switch (immediateBodyKind) {
        case TASK_BODY_TMD:
            modelObjectUnlinkTmd(&task->extra.tmd->link);
            modelObjectFreeTmd(task->extra.tmd);
            break;
        case TASK_BODY_COORD:
            modelObjectUnlinkCoordBody(&task->extra.coordBody->link);
            modelObjectFreeCoordBody(task->extra.coordBody);
            break;
    }
    _taskCollectImmediately(task);
}

Task* (taskSpawnFromTable)(TaskDesc* table, s32 index, TaskSpawnArg spawnArg1, TaskSpawnArg spawnArg2)
{
#ifdef __psyz
    if (table[index].callback == NULL) {
        fprintf(stderr, "task table %p index %d has no handler\n", (void*)table, (int)index);
        return NULL;
    }
#endif
    return _taskSpawnFromDesc(&table[index], spawnArg1, spawnArg2, _gTaskActiveList);
}

Task* (taskSpawn)(s32 bank, TaskSpawnArg selector, TaskSpawnArg spawnArg1, TaskSpawnArg spawnArg2)
{
    TaskDesc* desc;

    if (bank >= 0) {
        desc = gTaskDescBanks[bank];
#ifdef __psyz
        // Bit 31 of the selector is ignored by the PS1 build: its 32-bit index arithmetic wraps
        // (0x80000000 * sizeof(TaskDesc) is a multiple of 2^32). The room controller relies on this
        // for selector 0x80000007. Host pointers do not wrap, so drop the bit explicitly.
        desc = &desc[selector.unsignedValue & 0x7FFFFFFF];
#else
        desc = &desc[selector.value];
#endif
    } else {
        desc = selector.pointer;
    }
#ifdef __psyz
    if (desc->callback == NULL) {
        fprintf(stderr, "task spawn with no handler: bank=%d selector=%d desc=%p\n", (int)bank, (int)selector.value, (void*)desc);
    }
#endif
    return _taskSpawnFromDesc(desc, spawnArg1, spawnArg2, _gTaskActiveList);
}

/* Pop each owned child before dispatch. Exit handlers may free that child,
 * remove another child, or perform a nested walk; none of its bytes are read
 * afterwards. The parent remains live throughout this operation. */
void taskCallChildExits(Task* parentTask)
{
    Task* child;
    while ((child = parentTask->firstChild) != NULL) {
        _taskDetachForReparent(child);
        child->exitCallback(child);
    }
}

void taskCallExit(Task* task)
{
    task->exitCallback(task);
}

/* A parent reference need not be an owning ring membership: attachments
 * and projectiles also borrow their actor through this field. Only unlink an
 * actual member; retiring a borrowed reference must preserve other children. */
static inline void _taskDetachForReparent(Task* task)
{
    Task* parent = task->parent;
    Task* head = parent != NULL ? parent->firstChild : NULL;
    if (head != NULL) {
        Task* predecessor = head;
        do {
            if (predecessor->nextSibling == task) {
                if (task->nextSibling == task) {
                    parent->firstChild = NULL;
                } else {
                    predecessor->nextSibling = task->nextSibling;
                    if (head == task) {
                        parent->firstChild = task->nextSibling;
                    }
                }
                break;
            }
            predecessor = predecessor->nextSibling;
        } while (predecessor != head);
    }
    task->parent = NULL;
    task->nextSibling = task;
}

void taskSetParentReference(Task* task, Task* parent)
{
    _taskDetachForReparent(task);
    task->parent = parent;
}

void taskDetachFromParent(Task* task)
{
    _taskDetachForReparent(task);
}

void taskReparent(Task* newParent, Task* task)
{
    Task* childHead;
    Task* lastChild;
    Task* firstChild;

    // Restore the old parent's ring before changing the teardown relationship.
    _taskDetachForReparent(task);

    task->parent = newParent;
    childHead    = newParent->firstChild;
    if (childHead == NULL) {
        newParent->firstChild = task;
        return;
    }

    // Append after the last child, retaining the destination ring's head.
    lastChild  = childHead;
    firstChild = childHead;
    if (lastChild->nextSibling != lastChild) {
        do {
            lastChild = lastChild->nextSibling;
        } while (lastChild->nextSibling != firstChild);
    }
    task->nextSibling      = firstChild;
    lastChild->nextSibling = task;
}

void gameSetTaskSlot(struct Task* task, s32 slot)
{
    gGameSession->ptrSlots[slot] = task;
}

struct Task* gameGetTaskSlot(s32 slot)
{
    return gGameSession->ptrSlots[slot];
}

void taskInitList(TaskNode* listHead)
{
    _gTaskActiveList = listHead;
    listHead->next   = NULL;
    listHead->prev   = listHead;
}

/// Advances a task walk, collecting its cursor when body teardown is complete.
///
/// `cursor` must be non-NULL with readable body-kind and execution-link storage
/// after dispatch. A marked cursor must still be a linked primary-heap task
/// allocation with its work, body and teardown relationships already released.
/// Its predecessor is a bare head or a live task node; its successor is a live
/// task or NULL. The selected head must own the cursor when it is the tail.
/// `display` must remain live and writable through collection.
///
/// Saves the successor before unlinking and freeing a `TASK_BODY_RELEASED`
/// cursor, clearing `display->stopTaskWalk` only in that branch. Otherwise reads
/// the current successor without unlinking or freeing. Returns that borrowed
/// task, or NULL at the list end, without dispatching a callback. Callers consume
/// a stop request equal to one before reaching this helper. The dispatch
/// boundary excludes retired cursors before calling it.
static inline Task* _taskCollectReleasedAndAdvance(Task* cursor, DisplayState* display)
{
    Task* nextTask;

    if (cursor->bodyKind == TASK_BODY_RELEASED) {
        nextTask              = cursor->node.next;
        display->stopTaskWalk = 0;
        _taskUnlinkFromSelectedList(cursor);
        _taskFree(cursor);
        return nextTask;
    }
    return cursor->node.next;
}

static Task* _taskDispatchAndAdvance(Task* task, DisplayState* display, TaskFunc callback)
{
    TaskDispatchFrame frame = { _gTaskDispatchFrame, task, task->node.next };
    Task* next;
    _gTaskDispatchFrame = &frame;
    if (callback != NULL) {
        callback(task);
    }
    if (display->stopTaskWalk == TASK_WALK_STOP_REQUESTED) {
        display->stopTaskWalk = 0;
        next = NULL;
    } else if (frame.cursor == NULL) {
        next = frame.successor;
    } else {
        next = _taskCollectReleasedAndAdvance(frame.cursor, display);
    }
    _gTaskDispatchFrame = frame.previous;
    return next;
}

void taskExecList(TaskNode* listHead)
{
    Task*         currentTask;
    DisplayState* display;

    currentTask      = listHead->next;
    _gTaskActiveList = listHead;
    if (currentTask != NULL) {
        display = &gDisplayState;
    dispatchTask:
#ifdef __psyz
        gHostCurrentTaskCallback = (void*)currentTask->callback;
#endif
        currentTask = _taskDispatchAndAdvance(currentTask, display, currentTask->callback);
        if (currentTask != NULL) {
            goto dispatchTask;
        }
    }
}

TaskDesc* taskGetDesc(u32 bank, u32 index)
{
    TaskDesc* bankTable = gTaskDescBanks[bank];
    return bankTable + index;
}

TaskDesc* taskGetDescAt(TaskDesc* table, u32 index)
{
    return table + index;
}

void taskRequestKill(Task* task, s32 result)
{
    // Publish the result and suspend updates until the caller polls the request.
    task->status           = TASK_STATUS_STOP_REQUESTED;
    task->extraState.value = result;
    task->callback         = taskNoopCallback;
    taskCallChildExits(task);
}

bool taskPollKill(Task* task, s32* resultOut)
{
    bool exitDispatched;

    exitDispatched = false;
    if (task->status == TASK_STATUS_STOP_REQUESTED) {
        // Copy the result before the exit handler can release its storage.
        if (resultOut != NULL) {
            *resultOut = task->extraState.value;
        }
        task->exitCallback(task);
        exitDispatched = true;
    }
    return exitDispatched;
}

TaskNode* taskGetActiveList(void)
{
    return _gTaskActiveList;
}

void taskSetActiveList(TaskNode* listHead)
{
    _gTaskActiveList = listHead;
}

void taskResetDefaultList(void)
{
    _gTaskActiveList      = &gTaskDefaultList;
    gTaskDefaultList.next = NULL;
    gTaskDefaultList.prev = &gTaskDefaultList;
}

/// Removes a task's execution links using the selected head for a tail task.
///
/// The task must be live and linked. Its predecessor can be a bare head or an
/// embedded node. When it has no successor, the selected list must own it;
/// otherwise the successor supplies the backlink. Does not dispatch, release
/// resources or clear the removed task's own links.
static void _taskUnlinkFromSelectedList(Task* task)
{
    Task*      nextTask;
    TaskNode*  selectedListHead;
    TaskNode** backlinkSlot;
    TaskNode*  predecessorNode;

    nextTask              = task->node.next;
    selectedListHead      = _gTaskActiveList;
    backlinkSlot          = nextTask == NULL ? &selectedListHead->prev : &nextTask->node.prev;
    predecessorNode       = task->node.prev;
    *backlinkSlot         = predecessorNode;
    predecessorNode->next = task->node.next;
}

/// Releases only the primary-heap task allocation after execution-list unlinking.
///
/// Its owned work, body and teardown relationships must already be released.
/// No handlers run here; the pointer is invalid on return.
static void _taskFree(Task* task)
{
    _taskNotifyRetirement(task);
    memFree(task);
}

void taskExecDefaultList(TaskNode* unusedListHead)
{
    Task*         currentTask;
    DisplayState* display;

    currentTask      = gTaskDefaultList.next;
    _gTaskActiveList = &gTaskDefaultList;
    if (currentTask != NULL) {
        display = &gDisplayState;
    dispatchTask:
#ifdef __psyz
        if (currentTask->callback == NULL) {
            fprintf(stderr, "task %p has no callback; retiring it\n", (void*)currentTask);
            currentTask->callback = taskKill;
        }
#endif
        currentTask = _taskDispatchAndAdvance(currentTask, display, currentTask->callback);
        if (currentTask != NULL) {
            goto dispatchTask;
        }
    }
}

void taskExecListForPriority(TaskNode* listHead, s32 priority)
{
    Task*         currentTask;
    DisplayState* display;
    TaskNode*     savedListHead;
    s32           selectedPriority;

    currentTask      = listHead->next;
    savedListHead    = _gTaskActiveList;
    _gTaskActiveList = listHead;
    if (currentTask != NULL) {
        selectedPriority = priority & TASK_PRIORITY_MASK;
        display          = &gDisplayState;
    dispatchTask:
        currentTask = _taskDispatchAndAdvance(currentTask, display,
            currentTask->priority == selectedPriority ? currentTask->callback : NULL);
        if (currentTask != NULL) {
            goto dispatchTask;
        }
    }
    _gTaskActiveList = savedListHead;
}

void taskCallExitForPriority(TaskNode* listHead, s32 priority)
{
    Task*         currentTask;
    DisplayState* display;
    TaskNode*     savedListHead;
    s32           selectedPriority;

    currentTask      = listHead->next;
    savedListHead    = _gTaskActiveList;
    _gTaskActiveList = listHead;
    if (currentTask != NULL) {
        selectedPriority = priority & TASK_PRIORITY_MASK;
        display          = &gDisplayState;
    dispatchTask:
        currentTask = _taskDispatchAndAdvance(currentTask, display,
            currentTask->priority == selectedPriority ? taskCallExit : NULL);
        if (currentTask != NULL) {
            goto dispatchTask;
        }
    }
    _gTaskActiveList = savedListHead;
}

void taskCountdownCallback(Task* task)
{
    TmdObject* model;

    task->killCountdown--;
    if (task->killCountdown != 0) {
        return;
    }

    // Releasing dispatch. A model is unlinked, then freed; a coordinate body is
    // freed with its link unchanged.
    switch (task->bodyKind) {
        case TASK_BODY_TMD:
            model = task->extra.tmd;
            modelObjectUnlinkTmd(&model->link);
            modelObjectFreeTmd(model);
            task->bodyKind = TASK_BODY_RELEASED;
            break;
        case TASK_BODY_COORD:
            modelObjectFreeCoordBody(task->extra.coordBody);
            task->bodyKind = TASK_BODY_RELEASED;
            break;
        default:
            task->bodyKind = TASK_BODY_RELEASED;
            break;
    }
}

void gameClearTaskSlots(void)
{
    s32 slot;

    for (slot = (s32)ARRAY_SIZE(gGameSession->ptrSlots) - 1; slot >= 0; slot--) {
        gGameSession->ptrSlots[slot] = NULL;
    }
}

void memCopyBytes(const void* source, void* destination, u32 sizeBytes)
{
    /// Only the low halfword of the requested byte count is copied.
    enum { MEMORY_COPY_BYTE_COUNT_MASK = 0xFFFF };

    u32       tailByteIndex;
    u32       sourceAlignment;
    const u8* sourceCursor;
    u8*       destinationCursor;
    u32       bytesRemaining;

    sourceCursor      = source;
    destinationCursor = destination;
    bytesRemaining    = sizeBytes;

    /// Copies and advances through the next source word boundary.
    ///
    /// Requires at least four remaining bytes and disjoint readable/writable
    /// regions with the same residue modulo four. Cursor/count arguments must
    /// be stable distinct lvalues outside those regions, with no evaluation
    /// side effects: const u8*, u8* and u32. Alignment is a u32 residue in 0..3,
    /// evaluated once and must equal the source address modulo four. The
    /// selected chunk advances both pointers by 4, 3, 2 or 1 bytes and subtracts
    /// that amount from the full u32 count. The caller also requires the low
    /// halfword to be at least four, so no borrow changes its high bits. A residue
    /// outside 0..3 changes nothing. Alignment is recomputed after each chunk.
    /// The casts select aligned word/halfword access widths; they do not
    /// describe a containing object. No locals or configuration are captured.
#define MEMORY_COPY_ALIGNED_CHUNK(sourceCursor, destinationCursor, bytesRemaining, sourceAlignment) \
    do {                                                                                            \
        switch ((sourceAlignment)) {                                                                \
            case 0:                                                                                 \
                *(u32*)(destinationCursor) = *(const u32*)(sourceCursor);                           \
                (sourceCursor)            += sizeof(u32);                                           \
                (destinationCursor)       += sizeof(u32);                                           \
                (bytesRemaining)          -= sizeof(u32);                                           \
                break;                                                                              \
                                                                                                    \
            case 1:                                                                                 \
                *(destinationCursor)++     = *(sourceCursor)++;                                     \
                *(u16*)(destinationCursor) = *(const u16*)(sourceCursor);                           \
                (sourceCursor)            += sizeof(u16);                                           \
                (destinationCursor)       += sizeof(u16);                                           \
                (bytesRemaining)          -= 1 + sizeof(u16);                                       \
                break;                                                                              \
                                                                                                    \
            case 2:                                                                                 \
                *(u16*)(destinationCursor) = *(const u16*)(sourceCursor);                           \
                (sourceCursor)            += sizeof(u16);                                           \
                (destinationCursor)       += sizeof(u16);                                           \
                (bytesRemaining)          -= sizeof(u16);                                           \
                break;                                                                              \
                                                                                                    \
            case 3:                                                                                 \
                *(destinationCursor) = *(sourceCursor);                                             \
                (sourceCursor)      += 1;                                                           \
                (destinationCursor) += 1;                                                           \
                (bytesRemaining)    -= 1;                                                           \
                break;                                                                              \
        }                                                                                           \
    } while (0)

    while ((bytesRemaining & MEMORY_COPY_BYTE_COUNT_MASK) >= sizeof(u32)) {
        // Wide accesses follow source alignment; the destination must share it.
        sourceAlignment = (uintptr)sourceCursor & (sizeof(u32) - 1);

        MEMORY_COPY_ALIGNED_CHUNK(sourceCursor, destinationCursor, bytesRemaining, sourceAlignment);
    }

#undef MEMORY_COPY_ALIGNED_CHUNK

    // Fewer than four effective bytes remain; finish without wide accesses.
    bytesRemaining &= MEMORY_COPY_BYTE_COUNT_MASK;
    tailByteIndex   = 0;
    while ((tailByteIndex & MEMORY_COPY_BYTE_COUNT_MASK) < bytesRemaining) {
        *destinationCursor++ = *sourceCursor++;
        tailByteIndex++;
    }
}
