/*
Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/
#pragma once

#include "arith.h"

constexpr int N = 5; // number of verifiers
constexpr int T = 1; // corruption threshold (<= T corruptions) == polynomial degree
constexpr int K = 3; // degree of the extension field
constexpr int REPETITIONS = (40 + K - 1)/K; // Number of repetitions to get statistical security
constexpr int PREPROCESSING_REPETITIONS = REPETITIONS; // Number of linear combinations to do to check the preprocessing

static_assert((__uint128_t(1)<<K) >= N + 1, "Extension field is too small");
static_assert(N >= 4*T + 1, "Too many potential corruptions for the given number of players");

using ShareEl = GF2k<K>;

#if defined(PERFORM_TIMING)
    constexpr size_t N_TIMING_RUNS = 200;
#endif
