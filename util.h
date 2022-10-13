/*
Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

using Data = std::vector<uint8_t>;

inline void INT_TO_BYTES(uint8_t *buff, int x)
{
    buff[0]= x & 255;
    buff[1]= (x >> 8) & 255;
    buff[2]= (x >> 16) & 255;
    buff[3]= (x >> 24) & 255;
}

inline int BYTES_TO_INT(uint8_t *buff)
{
    return buff[0] + 256 * buff[1] + 65536 * buff[2] + 16777216 * buff[3];
}

Data Hash(const Data& data);

void complain_cheaters(const std::vector<int>& cheaters, const std::string& msg);
