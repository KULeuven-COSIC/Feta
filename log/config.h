/*
Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/
#pragma once

#include "arith.h"

constexpr int N = 4; // number of verifiers
constexpr int T = 1; // corruption threshold (<= T corruptions) == polynomial degree
constexpr int K = 3; // degree of the extension field, need this to be big enough to fit our batch size
constexpr int K_EXT = 87; // degree of the extension field in which to perform the multiplication checks
constexpr int COMPRESSION = 2; // The number of multiplication triples to combine when compressing
                               // Note that the extra communication is log_{COMPRESSION}(n) * (2 * COMPRESSION - 1)
constexpr int PREPROCESSING_REPETITIONS = (40 + K - 1)/K; // Number of linear combinations to do to check the preprocessing
constexpr int PREPROCESSING_REPETITIONS_EXT = (40 + K_EXT - 1)/K_EXT;

static_assert(K_EXT >= K && K_EXT % K == 0, "Multiplication check field must be an extension of the share field");
static_assert((__int128_t(1)<<std::min(K, 126)) >= N + 1, "Extension field is too small");
static_assert(N >= 3*T + 1, "Too many potential corruptions for the given number of players");

using ShareEl = GF2k<K>;
using CheckEl = GF2k<K_EXT>;

#if defined(PERFORM_TIMING)
    constexpr size_t N_TIMING_RUNS = 200;
#endif
#define PREPROCESSING_SECOND_FIELD
