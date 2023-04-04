/*
    Original code by Mike Cancilla (https://github.com/mikecancilla)
    2019

    This software is provided 'as-is', without any express or implied
    warranty. In no event will the authors be held liable for any
    damages arising from the use of this software.

    Permission is granted to anyone to use this software for any
    purpose, including commercial applications, and to alter it and
    redistribute it freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must
    not claim that you wrote the original software. If you use this
    software in a product, an acknowledgment in the product documentation
    would be appreciated but is not required.

    2. Altered source versions must be plainly marked as such, and
    must not be misrepresented as being the original software.

    3. This notice may not be removed or altered from any source
    distribution.
*/

#pragma once

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <cassert>

inline uint16_t read2Bytes(uint8_t *p)
{
    uint16_t ret = *p++;
    ret <<= 8;
    ret |= *p++;

    return ret;
}

inline uint32_t read3Bytes(uint8_t* p)
{
    uint32_t ret = 0;
    uint32_t val = *p++;
    ret |= val << 16;
    val = *p++;
    ret |= val << 8;
    ret |= *p;

    return ret;
}

inline uint32_t read4Bytes(uint8_t *p)
{
    uint32_t ret = 0;
    uint32_t val = *p++;
    ret = val << 24;
    val = *p++;
    ret |= val << 16;
    val = *p++;
    ret |= val << 8;
    ret |= *p;

    return ret;
}

inline size_t incrementPtr(uint8_t *&p, size_t bytes)
{
    p += bytes;
    return bytes;
}

// Search for 0x00 00 01
size_t inline nextStartCode(uint8_t *&p, size_t dataLength = -1)
{
    size_t count = 0;
    uint8_t *pStart = p;

    while(    *p != 0 ||
          *(p+1) != 0 ||
          *(p+2) != 1 &&
          count < dataLength)
    {
        incrementPtr(p, 1);
        count++;
    }

    if (0 == count)
        return count;

    return p - pStart;
}

// Search for 0x00 00 00 01
size_t inline nextNaluStartCode(uint8_t *&p, size_t dataLength = -1)
{
    size_t count = 0;
    uint8_t *pStart = p;

    while(    *p != 0 ||
          *(p+1) != 0 ||
          *(p+2) != 0 ||
          *(p+3) != 1 &&
          count < dataLength)
    {
        incrementPtr(p, 1);
        count++;
    }

    if (0 == count)
        return count;

    return p - pStart;
}

inline size_t skipToNextStartCode(uint8_t *&p)
{
    uint8_t *pStart = p;

    uint32_t fourBytes = read4Bytes(p);
    incrementPtr(p, 4);

    nextStartCode(p);

    return p - pStart;
}

inline size_t validateStartCode(uint8_t *&p, uint32_t startCode)
{
    uint32_t fourBytes = read4Bytes(p);
    incrementPtr(p, 4);

    uint32_t start_code_prefix = (fourBytes & 0xFFFFFF00) >> 8;
    assert(0x000001 == start_code_prefix);

    fourBytes &= 0x000000FF;
    assert(fourBytes == startCode);

    return 4;
}
