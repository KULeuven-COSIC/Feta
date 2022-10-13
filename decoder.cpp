/*
Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/
#ifdef NDEBUG // This is meant for testing, always enable assertions
#undef NDEBUG
#endif

#include "log/config.h"

#include <array>
#include <cassert>
#include <iostream>
#include <utility>
#include <vector>

#include "decoder.h"
#include "arith.h"



/****** Main driver for some testing ******/
int main() {
    std::srand(42);
    std::array<GF2k<K_EXT>, N> xcoords;
    for (int i = 0; i < N; i++) xcoords[i] = liftGF<K_EXT>(GF2k<K>(i + 1));
    for (int i = 0; i < 1000; i++) {
        std::array<ShareEl, T+1> poly;
        for (int j = 0; j < T+1; j++) poly[j] = ShareEl(rand());

        std::array<ShareEl, N> shares = encode<T, K, N>(poly);
        std::array<GF2k<K_EXT>, N> lifted;
        for (int i = 0; i < N; i++) lifted[i] = liftGF<K_EXT>(shares[i]);
        int n_errors = rand() % (T + 1);
        for (int j = 0; j < n_errors; j++) {
            lifted[rand() % N] = GF2k<K_EXT>(rand());
        }

        auto recovered = detail::berlekamp_welch<T, T>(xcoords, lifted);

        for (int j = 0; j < T + 1; j++) {
            assert(liftGF<K_EXT>(poly[j]) == recovered[j]);
        }
    }
}
