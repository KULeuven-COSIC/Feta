/*
Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/
#pragma once

#include "arith.h"

constexpr int N = 4; // number of verifiers
constexpr int T = 1; // corruption threshold (<= T corruptions) == polynomial degree
constexpr int K = 27; // degree of the extension field, need this to be big enough to fit our batch size
constexpr int FULL_REPETITIONS = 3; // Number of repetitions "full" repetitions, with different random polynomials (ρ)
constexpr int SZ_REPETITIONS = 2; // Number of different values ζ for Schwartz-Zippel checks, per FULL_REPETITIONS
constexpr int PREPROCESSING_REPETITIONS = (40 + K - 1)/K; // Number of linear combinations to do to check the preprocessing

static_assert((__int128_t(1)<<std::min(K, 126)) >= N + 1, "Extension field is too small");
static_assert(N >= 3*T + 1, "Too many potential corruptions for the given number of players");

using ShareEl = GF2k<K>;

#if defined(PERFORM_TIMING)
    constexpr size_t N_TIMING_RUNS = 200;
#endif
