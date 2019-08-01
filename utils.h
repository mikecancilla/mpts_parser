#pragma once

#include <cstdint>

inline uint16_t read_2_bytes(uint8_t *p);
inline uint32_t read_4_bytes(uint8_t *p);
size_t inline increment_ptr(uint8_t *&p, size_t bytes);