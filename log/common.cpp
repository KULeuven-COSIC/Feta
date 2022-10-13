/*
Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/
#include "config.h"

#include <vector>

/**
 * Randomize the multiplication triples x_i * y_i = z_i by r_i to the inner product triple
 *  <{r_i x_i}_i, {y_i}_i> = \sum_i r_i z_i
 *
 *  r_i are generated here by `gen`, which is assumed to be pre-seeded in a Fiat-Shamir style transformation
 *  or by a challenge from a verifier
 *
 * x_i gets changed to r_i x^i, *in place*
 *
 * Returns \sum_i r_i z_i
 */
CheckEl randomize_to_inner_product(std::vector<CheckEl>& xs, const std::vector<CheckEl>& zs, PRNG& gen) {
    assert(xs.size() == zs.size());
    CheckEl res{0};
    for (std::size_t i = 0; i < xs.size(); i++) {
        CheckEl r = CheckEl::random(gen);
        xs[i] *= r;
        res += zs[i] * r;
    }
    return res;
}

