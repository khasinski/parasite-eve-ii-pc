#ifndef PE_PORT_FIXED_MATH_H
#define PE_PORT_FIXED_MATH_H
#include <stdint.h>

/* PS1 integer operations: preserve wrapping bits and arithmetic right shifts.
 * Variable shift counts use the MIPS low-five-bit convention. */
static inline int32_t peShiftLeftWrap32(int32_t value, unsigned count)
{
    return (int32_t)((uint32_t)value << (count & 31u));
}

static inline int32_t peShiftRightSigned32(int32_t value, unsigned count)
{
    count &= 31u;
    uint32_t bits = (uint32_t)value >> count;
    if (value < 0 && count != 0) bits |= UINT32_MAX << (32u - count);
    return (int32_t)bits;
}

static inline int32_t peSignExtend16(uint32_t value)
{
    uint32_t low = value & UINT32_C(0xffff);
    return low < UINT32_C(0x8000) ? (int32_t)low : (int32_t)low - INT32_C(65536);
}
#endif
