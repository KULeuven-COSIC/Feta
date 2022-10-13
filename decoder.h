/*
Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/
#pragma once

#include <array>
#include <cassert>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

#include "arith.h"

class invalid_sharing : public std::runtime_error {
    public:
        invalid_sharing(const char* what) : std::runtime_error(what) {}
};


namespace detail {
    /****** Polynomial stuff ******/

    template <int Lf, int Lg, int k>
    std::array<GF2k<k>, Lf - Lg + 1> poly_div(std::array<GF2k<k>, Lf>& f, const std::array<GF2k<k>, Lg>& g) {
        std::array<GF2k<k>, Lf - Lg + 1> res{GF2k<k>(0)};
        // Find the most significant coefficient of the divisor
        GF2k<k> div;
        int MSC = -1;
        for (int i = Lg - 1; i >= 0; i--) {
            if (g[i] != GF2k<k>(0)) {
                div = g[i].inv();
                MSC = i;
                break;
            }
        }
        if (MSC < 0) {
            throw invalid_sharing("Division by zero polynomial");
        }
        // Everything that would result in a larger output poly should be 0 instead
        const auto f_end = Lf - Lg + MSC + 1;
        for (int i = f_end; i < Lf; i++) {
            if (f[i] != GF2k<k>(0)) {
                throw invalid_sharing("Output degree too large");
            }
        }

        for (int i = f_end - 1; i >= MSC; i--) {
            GF2k<k> el = div * f[i];
            res[i - MSC] = el;
            for (int j = 0; j < MSC; j++) {
                f[i - MSC + j] -= el * g[j];
            }
        }
        for (int x = 0; x < MSC; x++) {
            if (f[x] != GF2k<k>(0)) { // Make sure there's no remainder, otherwise reconstruction completely failed
                throw invalid_sharing("Non-zero remainder after polynomial division");
            }
        }
        return res;
    }
} // namespace detail

/****** Encoding ******/

/**
 * Evaluate the degree `L - 1` polynomial in `x`
 */
template <unsigned long L, int k>
GF2k<k> poly_eval(const std::array<GF2k<k>, L>& poly, GF2k<k> x) {
    GF2k<k> pt(1);
    GF2k<k> res(0);
    for (GF2k<k> c : poly) {
        res += c * pt;
        pt *= x;
    }
    return res;
}

/**
 * Encode a degree T polynomial by evaluating it in N points (1, ..., N)
 **/
template <int D, int k, std::size_t N>
std::array<GF2k<k>, N> encode(const std::array<GF2k<k>, D+1>& message) {
    std::array<GF2k<k>, N> res;
    for (std::size_t i = 1; i <= N; i++) {
        res[i - 1] = poly_eval<D + 1, k>(message, GF2k<k>(i));
    }
    return res;
}

/**
 * Encode a degree T polynomial by evaluating it in N points `xcoords`
 **/
template <int D, int k, std::size_t N>
std::array<GF2k<k>, N> encode(const std::array<GF2k<k>, N>& xcoords, const std::array<GF2k<k>, D+1>& message) {
    std::array<GF2k<k>, N> res;
    for (std::size_t i = 0; i < N; i++) {
        res[i] = poly_eval<D + 1, k>(message, xcoords[i]);
    }
    return res;
}

/******* Solving the linear system for Berlekamp-Welch ******/

namespace detail {
    template <int L, int k_>
    std::array<GF2k<k_>, L> solve(std::array<std::array<GF2k<k_>, L>, L>& M, std::array<GF2k<k_>, L>& y) {
        int row = 0;
        for (int col = 0; col < L; col++) {
            if (M[row][col] == GF2k<k_>(0)) {
                // Find another row to pivot
                bool found = false;
                for (int j = row + 1; j < L; j++) {
                    if (M[j][col] != GF2k<k_>(0)) {
                        found = true;
                        std::swap(M[row], M[j]);
                        std::swap(y[row], y[j]);
                        break;
                    }
                }
                if (!found) {
                    // No pivot found, continue to the next column, stay on this row
                    continue;
                }
            }

            GF2k<k_> t = M[row][col].inv();
            for (int j = col; j < L; j++) M[row][j] *= t;
            y[row] *= t;
            for (int j = 0; j < L; j++) {
                if (j == row) continue;
                GF2k<k_> m = M[j][col];
                for (int k = 0; k < L; k++) M[j][k] -= m * M[row][k];
                y[j] -= m * y[row];
            }
            row++;
        }
        for (int i = row; i < L; i++) {
            if (y[i] != GF2k<k_>(0))
                throw invalid_sharing("Linear system is inconsistent");
        }
        std::array<GF2k<k_>, L> res{GF2k<k_>(0)};
        int col = 0;
        for (int i = 0; i < L; i++) {
            while (col < L && M[i][col] == GF2k<k_>(0)) col++;
            if (col >= L) break;
            res[col] = y[i];
        }
        return res;
    }
} // namespace detail

/****** Decoder(s) ******/

namespace detail {
    template <int D, int E, int k, std::size_t N>
    std::array<GF2k<k>, D+1> berlekamp_welch(const std::array<GF2k<k>, N>& xcoords, const std::array<GF2k<k>, N>& shares) {
        // deg(f2) = E
        // deg(f1/f2) = def(f1) - def(f2) = D
        // => deg(f1) = D + E
        // => (D + E + 1) + (E + 1) coefficients, minus 1 for fixed constant term of f2
        static_assert(N >= D + 2*E + 1, "Cannot do error recovery with given parameters");
        std::array<std::array<GF2k<k>, D + 2*E + 1>, D + 2*E + 1> M;
        std::array<GF2k<k>, D + 2*E + 1> y;
        for (int i = 0; i < D + 2*E + 1; i++) {
            // Coefficients of f1(x)
            GF2k<k> a(1);
            for (int j = 0; j < D + E + 1; j++) {
                M[i][j] = a;
                a *= xcoords[i];
            }
            
            // Coefficients of f2(x); f2(0) = 1
            a = GF2k<k>(1);
            for (int j = D + E + 1; j < D + 2*E + 1; j++) {
                a *= xcoords[i];
                M[i][j] = shares[i] * a;
            }

            
            y[i] = shares[i];
        }
        auto sol = solve<D + 2*E + 1, k>(M, y);
        std::array<GF2k<k>, D+E+1> f1;
        for (int i = 0; i < D+E+1; i++) {
            f1[i] = sol[i];
        }
        std::array<GF2k<k>, E+1> f2;
        f2[0] = GF2k<k>(1);
        for (int i = D+E+1; i < D+2*E+1; i++) {
            f2[i - D - E] = sol[i];
        }
        return poly_div<D + E + 1, E + 1, k>(f1, f2);
    }

    template <int k>
    GF2k<k> lagrange_l(const std::int64_t& lo, const std::int64_t& hi, const std::int64_t& j, GF2k<k> x) {
        GF2k<k> res{1};
        GF2k<k> denom{1};
        GF2k<k> el_j{j};
        for (std::int64_t m = lo; m < hi; m++) {
            if (m == j) continue;
            GF2k<k> el_m{m};
            res *= (x - el_m);
            denom *= (el_j - el_m);
        }
        return res * denom.inv();
    }

    template <int k>
    GF2k<k> lagrange_l(const std::vector<GF2k<k>>& xcoords, GF2k<k> coord, GF2k<k> x) {
        GF2k<k> res{1};
        GF2k<k> denom{1};
        for (const auto& m : xcoords) {
            if (m == coord) continue;
            res *= (x - m);
            denom *= (coord - m);
        }
        return res * denom.inv();
    }
} // namespace detail

/**
 * Decode a collection of N shares (in order) back into the original polynomial
 * Also indicate a set of cheating parties
 *
 * Throws invalid_sharing if this cannot be done
 **/
template <int D, int E, int k, std::size_t N>
std::pair<std::array<GF2k<k>, D + 1>, std::vector<int>> decode(const std::array<GF2k<k>, N>& xcoords, const std::array<GF2k<k>, N>& shares) {
    static_assert(N > D + 2*E, "Cannot do error recovery with given parameters");
    auto poly = detail::berlekamp_welch<D, E>(xcoords, shares);
    auto recovered = encode<D, k, N>(xcoords, poly);
    std::vector<int> cheaters;
    for (std::size_t i = 0; i < N; i++) {
        if (shares[i] != recovered[i]) {
            cheaters.push_back(i + 1);
        }
    }
    return {poly, cheaters};
}

template <int D, int E, int k, std::size_t N>
std::pair<std::array<GF2k<k>, D + 1>, std::vector<int>> decode(const std::array<GF2k<k>, N>& shares) {
    static_assert(N > D + 2*E, "Cannot do error recovery with given parameters");

    static bool xcoords_init = false;
    static std::array<GF2k<k>, N> xcoords;
    if (!xcoords_init) {
        for (std::size_t i = 0; i < N; i++) xcoords[i] = GF2k<k>(i + 1);
        xcoords_init = true;
    }

    auto poly = detail::berlekamp_welch<D, E>(xcoords, shares);
    auto recovered = encode<D, k, N>(xcoords, poly);
    std::vector<int> cheaters;
    for (std::size_t i = 0; i < N; i++) {
        if (shares[i] != recovered[i]) {
            cheaters.push_back(i + 1);
        }
    }
    return {poly, cheaters};
}

template <int k>
GF2k<k> interpolate(const std::vector<GF2k<k>>& ys, const GF2k<k>& x) {
    assert(ys.size() < (1ull << std::min(k, 63)));
    GF2k<k> res{0};
    for (std::size_t i = 0; i < ys.size(); i++) {
        res += ys[i] * detail::lagrange_l<k>(0, ys.size(), i, x);
    }
    return res;
}

template <int k>
std::vector<GF2k<k>> interpolate_preprocess(const std::vector<GF2k<k>>& xcoords, GF2k<k> x) {
    // Deciding between:
    //   - do preprocessing with x-value + num interpolation points ⇒ vector of multipliers
    //      for prover: (n2 + 2ρ) different x-values, each used 2*n1 times
    //                    O((n2 + ρ)^3) preprocessing (num interpolation points * degree * num multipliers)
    //                  + O(n1 * (n2 + ρ)^2) evaluation (num interpolations * inner product ys x multipliers)
    //   - do precompuation for num interpolation points only ⇒ vector of polynomials
    //      for prover: O((n2 + ρ)^2) preprocessing (degree * num basis polynomials)
    //                + O(n1 * (n2 + ρ)^2) evaluation (num interpolations * (inner product + eval))
    //  The real difference: where the eval happens (and some implementation stuff), either doing
    //   - num interpolations different evals (online); or
    //   - degree interpolations (preprocessing)
    //   The latter is slightly better wrt the prover, the former wrt the verifiers (prover needs many more interpolations:
    //      O(n1 * (n2 + ρ)) for the prover vs O(ρ*n1) for the verifiers)
    //   if O(ρ * n1) == O(n2 + ρ), the difference is negligible for the verifier (which makes sense for n1 ~ n2 ~ sqrt(n_S) and ρ constant)
    //
    //   Hence, we prefer the first option: generating a vector of multipliers per x coordinate
    //
    //  Original situation for prover:
    //      O(1) preprocessing (none, really)
    //    + O(n1 * (n2 + ρ)^3) evaluation (num interpolations * build/eval polynomial * inner product)
    
    std::vector<GF2k<k>> res;
    res.reserve(xcoords.size());
    for (GF2k<k> coord : xcoords) {
        res.push_back(detail::lagrange_l<k>(xcoords, coord, x));
    }
    return res;
}

template <int k>
std::vector<GF2k<k>> interpolate_preprocess(unsigned npoints, GF2k<k> x) {
    assert(npoints < (1ull << std::min(k, 63)));
    std::vector<GF2k<k>> res;
    for (int i = 0; i < npoints; i++) {
        res.emplace_back(detail::lagrange_l<k>(0, npoints, i, x));
    }
    return res;
}

template <int k>
GF2k<k> interpolate_with_preprocessing(const std::vector<GF2k<k>>& preprocessing, const std::vector<GF2k<k>>& ys) {
    assert(ys.size() == preprocessing.size());
    GF2k<k> res{0};
    for (std::size_t i = 0; i < ys.size(); i++) {
        res += ys[i] * preprocessing[i];
    }
    return res;
}

template <unsigned long N, int k>
std::array<GF2k<k>, N> interpolate_poly(const std::array<GF2k<k>, N>& vals) {
    static_assert(N == 2, "Can currently only handle interpolation on 2 points"); // TODO: improve
    return {vals[0], vals[1] - vals[0]};
}

template <unsigned long N1, unsigned long N2, int k>
std::array<GF2k<k>, N1+N2 - 1> poly_mul(const std::array<GF2k<k>, N1>& x, const std::array<GF2k<k>, N2>& y) {
    std::array<GF2k<k>, N1+N2-1> res{GF2k<k>{0}};
    for (unsigned long i = 0; i < N1; i++) {
        for (unsigned long j = 0; j < N2; j++) {
            res[i + j] += x[i] * y[j];
        }
    }
    return res;
}
