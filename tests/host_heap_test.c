#include "port/heap.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(int argc, char** argv)
{
    InitHeap3(NULL, 0); /* The native adapter does not use console arenas. */
    volatile unsigned char* first = malloc3(32);
    unsigned char* second = malloc3(32);
    assert(first && second && (void*)first != second);
    assert((uintptr_t)first % _Alignof(max_align_t) == 0);
    memset((void*)first, 0x37, 32);
    memset(second, 0x91, 32);
    if (argc > 1 && !strcmp(argv[1], "overrun")) {
        first[32] = 1; /* Negative sanitizer control, never run by the default test. */
    }
    free3((void*)first);
    if (argc > 1 && !strcmp(argv[1], "retired")) {
        return first[0]; /* Negative sanitizer control. */
    }
    for (unsigned i = 0; i < 32; ++i) assert(second[i] == 0x91);
    free3(second);
    free3(NULL);
    puts("Native heap: SDK-compatible aligned allocations have independent lifetimes");
    return 0;
}
