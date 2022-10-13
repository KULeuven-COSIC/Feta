/*
Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/
#include "util.h"

#include <iostream>

#include "openssl/sha.h"

Data Hash(const Data& data) {
    Data res(SHA256_DIGEST_LENGTH, 0);
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data.data(), data.size());
    SHA256_Final(res.data(), &sha256);
    return res;
}

void complain_cheaters(const std::vector<int>& cheaters, const std::string& msg) {
    if (!cheaters.empty()) {
        std::cout << "The following parties tried to cheat on " << msg << ": ";
        for (int c : cheaters) {
            std::cout << c << " ";
        }
        std::cout << std::endl;
    }
}

