/* Native heap3 adapter. Each allocation is a libc allocation, giving ASan
 * per-block bounds and retirement checks without an emulated arena. */
#include "port/heap.h"
#include <stdlib.h>

void* _freep; /* Legacy heap selection cursor; ignored by the native adapter. */

void InitHeap3(void* head, long size)
{
    (void)head;
    (void)size;
}

void* malloc3(size_t size)
{
    return malloc(size);
}

void free3(void* allocation)
{
    free(allocation);
}
