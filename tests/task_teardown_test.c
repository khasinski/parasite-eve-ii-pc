#ifdef NDEBUG
#undef NDEBUG
#endif
#include "main/task.h"
#include "../src/main/task.h"
#include "main/display_types.h"
#include "main/mem.h"
#include "gameplay/model_objects.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DisplayState gDisplayState;
static unsigned freed;
void taskNoopCallback(Task* unused) { (void)unused; }

/* Model the host allocator's cleared retired task storage. Sanitized runs also
 * catch any read after free; ordinary runs reproduce the observed NULL link. */
void memFree(void* allocation)
{
    assert(allocation);
    memset(allocation, 0, sizeof(Task));
    free(allocation);
    ++freed;
}
void modelObjectUnlinkTmd(TmdListNode* node) { (void)node; abort(); }
void modelObjectFreeTmd(TmdObject* model) { (void)model; abort(); }
void modelObjectUnlinkCoordBody(TmdListNode* node) { (void)node; abort(); }
void modelObjectFreeCoordBody(ModelObjectCoordBody* body) { (void)body; abort(); }

static void retire_child(Task* task)
{
    assert(task->parent == NULL);
    taskKill(task);
}

static Task* make_task(void)
{
    Task* task = calloc(1, sizeof(*task));
    assert(task);
    task->callback = taskNoopCallback;
    task->exitCallback = retire_child;
    task->nextSibling = task;
    /* Execution lists are linear, with a bare head and a tail backlink. */
    task->node.prev = gTaskDefaultList.prev;
    gTaskDefaultList.prev->next = task;
    gTaskDefaultList.prev = &task->node;
    return task;
}

static void run(unsigned children, int nested, int immediate)
{
    memset(&gDisplayState, 0, sizeof(gDisplayState));
    gDisplayState.immediateTaskFree = immediate;
    gTaskDefaultList.next = NULL;
    gTaskDefaultList.prev = &gTaskDefaultList;
    freed = 0;
    Task* parent = make_task();
    Task* first = make_task();
    parent->firstChild = first;
    first->parent = parent;
    if (children == 2) {
        Task* second = make_task();
        second->parent = parent;
        first->nextSibling = second;
        second->nextSibling = first;
    }
    if (nested) {
        Task* grandchild = make_task();
        first->firstChild = grandchild;
        grandchild->parent = first;
    }
    taskKill(parent);
    unsigned expected = 1 + children + nested;
    if (!immediate) {
        assert(freed == 0);
        taskExecList(&gTaskDefaultList);
    }
    assert(freed == expected);
    assert(gTaskDefaultList.next == NULL);
    assert(gTaskDefaultList.prev == &gTaskDefaultList);
}

static void reset(void)
{
    memset(&gDisplayState, 0, sizeof gDisplayState);
    gDisplayState.immediateTaskFree = 1;
    taskInitList(&gTaskDefaultList);
    freed = 0;
}

static void run_child_dispatch(int request)
{
    reset();
    Task* parent = make_task();
    Task* first = make_task();
    Task* second = make_task();
    taskReparent(parent, first);
    taskReparent(parent, second);
    if (request) {
        s32 result = 0;
        taskRequestKill(parent, 42);
        assert(freed == 2 && parent->firstChild == NULL);
        assert(taskPollKill(parent, &result) && result == 42);
    } else {
        taskCallChildExits(parent);
        assert(freed == 2 && parent->firstChild == NULL);
        taskKill(parent);
    }
    assert(freed == 3 && gTaskDefaultList.next == NULL);
}

static void run_borrowed_parent(void)
{
    reset();
    Task* parent = make_task();
    Task* first = make_task();
    Task* second = make_task();
    Task* borrowed = make_task();
    taskReparent(parent, first);
    taskReparent(parent, second);
    /* Compatibility with legacy spawns assigning a weak parent directly. */
    borrowed->parent = parent;
    taskKill(borrowed);
    assert(parent->firstChild == first && first->nextSibling == second);
    assert(second->nextSibling == first);
    borrowed = make_task();
    taskSetParentReference(borrowed, parent);
    taskReparent(parent, borrowed);
    assert(first->nextSibling == second && second->nextSibling == borrowed);
    taskSetParentReference(borrowed, parent);
    assert(parent->firstChild == first && second->nextSibling == first);
    taskKill(borrowed);
    taskKill(parent);
    assert(freed == 5 && gTaskDefaultList.next == NULL);
}

static void run_walk(int mode)
{
    reset();
    for (unsigned i = 0; i < 3; ++i) {
        Task* task = make_task();
        task->callback = retire_child;
        task->priority = 0x52;
    }
    switch (mode) {
        case 0: taskExecList(&gTaskDefaultList); break;
        case 1: taskExecDefaultList(NULL); break;
        case 2: taskExecListForPriority(&gTaskDefaultList, 0x52); break;
        case 3: taskCallExitForPriority(&gTaskDefaultList, 0x52); break;
    }
    assert(freed == 3 && gTaskDefaultList.next == NULL);
    assert(gTaskDefaultList.prev == &gTaskDefaultList);
}

static void retire_next_and_self(Task* task)
{
    taskKill(task->node.next);
    taskKill(task);
}

static void nested_exit_walk(Task* task)
{
    (void)task;
    taskCallExitForPriority(&gTaskDefaultList, 0x52);
}

static void run_nested_and_successor_retirement(void)
{
    reset();
    Task* first = make_task();
    first->callback = retire_next_and_self;
    make_task();
    Task* last = make_task();
    last->callback = retire_child;
    taskExecList(&gTaskDefaultList);
    assert(freed == 3 && gTaskDefaultList.next == NULL);
    reset();
    first = make_task();
    first->priority = 0x52;
    first->callback = nested_exit_walk;
    last = make_task();
    last->priority = 0x52;
    taskExecList(&gTaskDefaultList);
    assert(freed == 2 && gTaskDefaultList.next == NULL);
}

static void append_during_dispatch(Task* task)
{
    task->callback = taskNoopCallback;
    Task* appended = make_task();
    appended->callback = retire_child;
}

static void stop_after_retirement(Task* task)
{
    gDisplayState.stopTaskWalk = 1;
    taskKill(task);
}

static void run_walk_contracts(void)
{
    reset();
    Task* first = make_task();
    first->callback = append_during_dispatch;
    taskExecList(&gTaskDefaultList);
    assert(freed == 1 && gTaskDefaultList.next == first);
    taskKill(first);
    assert(freed == 2);

    reset();
    first = make_task();
    first->callback = stop_after_retirement;
    Task* second = make_task();
    second->callback = retire_child;
    taskExecList(&gTaskDefaultList);
    assert(freed == 1 && gTaskDefaultList.next == second);
    assert(gDisplayState.stopTaskWalk == 0);
    taskExecList(&gTaskDefaultList);
    assert(freed == 2 && gTaskDefaultList.next == NULL);

    reset();
    first = make_task();
    first->priority = 1;
    gDisplayState.immediateTaskFree = 0;
    taskKill(first); /* A nonmatching-priority task still needs collection. */
    gDisplayState.immediateTaskFree = 1;
    second = make_task();
    second->priority = 0x52;
    taskCallExitForPriority(&gTaskDefaultList, 0x52);
    assert(freed == 2 && gTaskDefaultList.next == NULL);
}

int main(void)
{
    run(1, 0, 1);
    run(2, 0, 1);
    run(2, 1, 1);
    run(2, 1, 0);
    run_child_dispatch(0);
    run_child_dispatch(1);
    run_borrowed_parent();
    for (int mode = 0; mode < 4; ++mode) run_walk(mode);
    run_nested_and_successor_retirement();
    run_walk_contracts();
    puts("Task child teardown: immediate, sibling ring, nested and deferred OK");
}
