#pragma once

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <cassert>

inline uint16_t read_2_bytes(uint8_t *p)
{
	uint16_t ret = *p++;
	ret <<= 8;
	ret |= *p++;

	return ret;
}

inline uint32_t read_4_bytes(uint8_t *p)
{
    uint32_t ret = 0;
    uint32_t val = *p++;
    ret = val<<24;
    val = *p++;
    ret |= val << 16;
    val = *p++;
    ret |= val << 8;
    ret |= *p;

    return ret;
}

size_t inline increment_ptr(uint8_t *&p, size_t bytes)
{
    p += bytes;
    return bytes;
}

size_t inline next_start_code(uint8_t *&p, size_t data_length = -1)
{
    size_t count = 0;
    uint8_t *pStart = p;

    while(    *p != 0 ||
          *(p+1) != 0 ||
          *(p+2) != 1 &&
          count < data_length)
    {
        increment_ptr(p, 1);
        count++;
    }

    if(-1 == count)
        return count;

    return p - pStart;
}

inline size_t skip_to_next_start_code(uint8_t *&p)
{
    uint8_t *pStart = p;

    uint32_t four_bytes = read_4_bytes(p);
    increment_ptr(p, 4);

    next_start_code(p);

    return p - pStart;
}

inline size_t validate_start_code(uint8_t *&p, uint32_t start_code)
{
    uint32_t four_bytes = read_4_bytes(p);
    increment_ptr(p, 4);

    uint32_t start_code_prefix = (four_bytes & 0xFFFFFF00) >> 8;
    assert(0x000001 == start_code_prefix);

    four_bytes &= 0x000000FF;
    assert(four_bytes == start_code);

    return 4;
}
