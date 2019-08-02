#pragma once

#include <cstdint>

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
