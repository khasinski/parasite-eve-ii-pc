#ifndef PE_PORT_HEAP_H
#define PE_PORT_HEAP_H
#include <stddef.h>

/* Host declarations use size_t even on LLP64 Windows. */
void InitHeap3(void* head, long size);
void* malloc3(size_t size);
void free3(void* allocation);
#endif
