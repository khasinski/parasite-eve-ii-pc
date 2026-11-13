#include "port/fixed_math.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

int main(void)
{
    for (uint32_t i = 0; i <= UINT16_MAX; ++i) {
        int32_t expected = i < 32768 ? (int32_t)i : (int32_t)i - 65536;
        assert(peSignExtend16(i) == expected);
        assert(peSignExtend16(i | UINT32_C(0xbeef0000)) == expected);
    }
    static const int32_t values[] = {INT32_MIN, INT32_MIN + 1, -65536, -32769,
        -32768, -4097, -4096, -1, 0, 1, 4095, 32767, 65535, INT32_MAX};
    for (size_t i = 0; i < sizeof(values)/sizeof(values[0]); ++i) {
        for (unsigned shift = 0; shift < 32; ++shift) {
            int64_t divisor = INT64_C(1) << shift;
            int64_t expected = values[i] / divisor;
            if (values[i] < 0 && values[i] % divisor) --expected;
            assert(peShiftRightSigned32(values[i], shift) == expected);
            assert((uint32_t)peShiftLeftWrap32(values[i], shift) == ((uint32_t)values[i] << shift));
            assert(peShiftRightSigned32(values[i], shift + 32) == expected);
        }
    }
    puts("Fixed-point integers: every signed halfword and wrapping/rounding boundaries checked");
    return 0;
}
