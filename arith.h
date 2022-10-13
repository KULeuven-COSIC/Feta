/*
Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/
#pragma once
#include <cassert>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <tuple>

#include <emmintrin.h>
#include <smmintrin.h>
#include <wmmintrin.h>

#include "gftables.h"
#include "random.h"

namespace detail {
    // Can't be constexpr, unfortunately
    class int128 {
        public:
            int128() : m_val(_mm_set_epi64x(0, 0)) {}
            int128(__m128i x) : m_val(std::move(x)) {}
            int128(long long x) : m_val(_mm_set_epi64x(0, x)) {}
            int128(long long lo, long long hi) : m_val(_mm_set_epi64x(hi, lo)) {}

            static int128 make_mask(int k) {
                if (k == 0) {
                    return 0;
                } else if (k < 64) {
                    return int128((1ull << k) - 1);
                } else if (k == 64) {
                    return int128(-1);
                } else if (k < 128) {
                    return int128(_mm_set_epi64x((1ull << (k - 64)) - 1, -1));
                } else if (k == 128) {
                    return int128(_mm_set_epi64x(-1, -1));
                } else {
                    __builtin_unreachable();
                }
            }

            int128 operator&(const int128& o) const {
                return m_val & o.m_val;
            }
            int128& operator&=(const int128& o) {
                return *this = *this & o;
            }

            int128 operator^(const int128& o) const {
                return m_val ^ o.m_val;
            }
            int128& operator^=(const int128& o) {
                return *this = *this ^ o;
            }

            int128 operator>>(int s) const {
                __m128i packed_shifted = _mm_srli_epi64(m_val, s); // Shift both parts
                __m128i cross_boundary = m_val;
                if (s < 64) {
                    cross_boundary = _mm_slli_epi64(m_val, 64 - s); // LSB of high to become MSB of low
                } else if (s > 64) {
                    cross_boundary = _mm_srli_epi64(m_val, s - 64); // MSB of high to become LSB of low
                }
                return packed_shifted ^ _mm_srli_si128(cross_boundary, 8); // Right shift the mixin by 8 *bytes* to bring high into low
            }
            int128& operator>>=(int s) {
                return *this = (*this) >> s;
            }

            int128 operator<<(int s) const {
                __m128i packed_shifted = _mm_slli_epi64(m_val, s); // Shift both parts
                __m128i cross_boundary = m_val;
                if (s < 64) {
                    cross_boundary = _mm_srli_epi64(m_val, 64 - s); // MSB of low to become LSB of high
                } else if (s > 64) {
                    cross_boundary = _mm_slli_epi64(m_val, s - 64); // LSB of low to become MSB of high
                }
                return packed_shifted ^ _mm_slli_si128(cross_boundary, 8); // Left shift by 8 bytes to bring low into high
            }
            int128& operator<<=(int s) {
                return *this = (*this) << s;
            }

            bool operator==(const int128& o) const {
                const __m128i tmp = m_val ^ o.m_val;
                return _mm_test_all_zeros(tmp, tmp);
            }
            bool operator!=(const int128& o) const {
                return !(*this == o);
            }

            bool operator<(const unsigned long long& other) { // Special case for this, because it's a pain to do with another int128
                return !(
                        _mm_test_all_zeros(m_val, _mm_set_epi64x(-1, 0))                  // if anything is in the top 64 bits, it's definitely bigger
                     || static_cast<unsigned long long>(_mm_cvtsi128_si64(m_val)) >= other // otherwise, unsigned compare the rest
                     );
            }

            std::int64_t low() const {
                return _mm_extract_epi64(m_val, 0);
            }

            std::int64_t high() const {
                return _mm_extract_epi64(m_val, 1);
            }

            explicit operator __m128i() const {
                return m_val;
            }
            __m128i reveal() const {
                return m_val;
            }

        private:
            __m128i m_val;
    };

    inline std::ostream& operator<<(std::ostream& os, const int128& x) {
        return os << std::hex << std::setfill('0') << std::setw(16) << x.high()
            << std::hex << std::setfill('0') << std::setw(16) << x.low();
    }

    template <typename T>
    T extract(const int128& x) {
        if constexpr(std::is_same_v<T, int128>) {
            return x;
        } else {
            return T(x.low());
        }
    }

    class int256 {
        public:
            int256(int128 lo, int128 hi) : m_lo(lo), m_hi(hi) {}

            int256 operator&(const int256& o) const {
                return int256(m_lo & o.m_lo, m_hi & o.m_hi);
            }
            int256 operator&(const int128& o) const {
                return int256(m_lo & o.reveal(), 0);
            }

            int256 operator^(const int256& o) const {
                return int256(m_lo ^ o.m_lo, m_hi ^ o.m_hi);
            }

            int256 operator>>(int s) const {
                assert(s < 128);
                int128 lo = (m_lo >> s) ^ (m_hi << (128 - s));
                return int256(lo, m_hi >> s);
            }
            int256 operator<<(int s) const {
                assert(s < 128);
                int128 hi = (m_hi << s) ^ (m_lo >> (128 - s));
                return int256(m_lo << s, hi);
            }

            int128 low() const {
                return m_lo;
            }
            int128 high() const {
                return m_hi;
            }

            static int256 make_mask(int k) {
                return {int128::make_mask(std::min(k, 128)), int128::make_mask(std::max(0, k - 128))};
            }
            
        private:
            int128 m_lo, m_hi;
    };

    template <int k>
    constexpr int type_idx() {
        static_assert(k > 0, "Sane values?");
        if (k <= 8) return 0;
        else if (k <= 16) return 1;
        else if (k <= 32) return 2;
        else if (k <= 64) return 3;
        else return 4;
    }

    template <int idx> struct datatype;
    template <> struct datatype<0> {using type = uint8_t;};
    template <> struct datatype<1> {using type = uint16_t;};
    template <> struct datatype<2> {using type = uint32_t;};
    template <> struct datatype<3> {using type = uint64_t;};
    template <> struct datatype<4> {using type = int128;};

    template <typename T, int k>
    constexpr T make_mask() {
        if constexpr(std::is_same_v<T, int128> || std::is_same_v<T, int256>) {
            return T::make_mask(k);
        } else {
            return (T(1) << (k % (8 * sizeof(T)))) - 1;
        }
    }

    template <int k>
    constexpr int num_reduction_monomials() {
        static_assert(2 <= k && k <= 128, "Unsupported extension field");
        int responses[] = {0, 0, 3, 3, 3, 3, 3, 3, 5, 3, 3, 3, 3, 5, 3, 3, 5, 3, 3, 5, 3, 3, 3, 3, 5, 3, 5, 5, 3, 3, 3, 3, 5, 3, 3, 3, 3, 5, 5, 3, 5, 3, 3, 5, 3, 5, 3, 3, 5, 3, 5, 5, 3, 5, 3, 3, 5, 3, 3, 5, 3, 5, 3, 3, 5, 3, 3, 5, 3, 5, 5, 3, 5, 3, 3, 5, 3, 5, 5, 3, 5, 3, 5, 5, 3, 5, 3, 3, 5, 3, 3, 5, 3, 3, 3, 3, 5, 3, 3, 5, 3, 5, 3, 3, 5, 3, 3, 5, 3, 5, 3, 3, 5, 3, 5, 5, 5, 5, 3, 3, 5, 3, 5, 3, 3, 5, 3, 3, 5};
        return responses[k];
    }

    // Exclude both the most significant monomial (since we know it's x^k) and the least significant (x^0)
    template <int k, int w>
    struct reduction_polynomial_impl {};

    template <int k>
    struct reduction_polynomial_impl<k, 3> {
        using type = int;
        constexpr static type value() {
            type responses[] = {{}, {}, 1, 1, 1, 2, 1, 1, 0, 1, 3, 2, 3, 0, 5, 1, 0, 3, 3, 0, 3, 2, 1, 5, 0, 3, 0, 0, 1, 2, 1, 3, 0, 10, 7, 2, 9, 0, 0, 4, 0, 3, 7, 0, 5, 0, 1, 5, 0, 9, 0, 0, 3, 0, 9, 7, 0, 4, 19, 0, 1, 0, 29, 1, 0, 18, 3, 0, 9, 0, 0, 6, 0, 25, 35, 0, 21, 0, 0, 9, 0, 4, 0, 0, 5, 0, 21, 13, 0, 38, 27, 0, 21, 2, 21, 11, 0, 6, 11, 0, 15, 0, 29, 9, 0, 4, 15, 0, 17, 0, 33, 10, 0, 9, 0, 0, 0, 0, 33, 8, 0, 18, 0, 2, 19, 0, 21, 1, 0};
            return responses[k];
        }
    };

    template <int k>
    struct reduction_polynomial_impl<k, 5> {
        using type = std::tuple<int, int, int>;
        constexpr static type value() {
            type responses[] = {{}, {}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {1, 3, 4}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {1, 3, 4}, {0, 0, 0}, {0, 0, 0}, {1, 3, 5}, {0, 0, 0}, {0, 0, 0}, {1, 2, 5}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {1, 3, 4}, {0, 0, 0}, {1, 3, 4}, {1, 2, 5}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {2, 3, 7}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {1, 4, 6}, {1, 5, 6}, {0, 0, 0}, {3, 4, 5}, {0, 0, 0}, {0, 0, 0}, {3, 4, 6}, {0, 0, 0}, {1, 3, 4}, {0, 0, 0}, {0, 0, 0}, {2, 3, 5}, {0, 0, 0}, {2, 3, 4}, {1, 3, 6}, {0, 0, 0}, {1, 2, 6}, {0, 0, 0}, {0, 0, 0}, {2, 4, 7}, {0, 0, 0}, {0, 0, 0}, {2, 4, 7}, {0, 0, 0}, {1, 2, 5}, {0, 0, 0}, {0, 0, 0}, {1, 3, 4}, {0, 0, 0}, {0, 0, 0}, {1, 2, 5}, {0, 0, 0}, {2, 5, 6}, {1, 3, 5}, {0, 0, 0}, {3, 9, 10}, {0, 0, 0}, {0, 0, 0}, {1, 3, 6}, {0, 0, 0}, {2, 5, 6}, {3, 5, 6}, {0, 0, 0}, {2, 4, 9}, {0, 0, 0}, {1, 3, 8}, {2, 4, 7}, {0, 0, 0}, {1, 2, 8}, {0, 0, 0}, {0, 0, 0}, {2, 6, 7}, {0, 0, 0}, {0, 0, 0}, {1, 5, 8}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {6, 9, 10}, {0, 0, 0}, {0, 0, 0}, {1, 3, 6}, {0, 0, 0}, {1, 6, 7}, {0, 0, 0}, {0, 0, 0}, {1, 3, 4}, {0, 0, 0}, {0, 0, 0}, {4, 7, 9}, {0, 0, 0}, {2, 4, 5}, {0, 0, 0}, {0, 0, 0}, {3, 4, 5}, {0, 0, 0}, {2, 3, 5}, {5, 7, 8}, {1, 2, 4}, {1, 2, 5}, {0, 0, 0}, {0, 0, 0}, {1, 3, 4}, {0, 0, 0}, {1, 2, 6}, {0, 0, 0}, {0, 0, 0}, {5, 6, 7}, {0, 0, 0}, {0, 0, 0}, {1, 2, 7}};
            return responses[k];
        }
    };

    template <int k>
    constexpr auto reduction_polynomial() {
        return reduction_polynomial_impl<k, num_reduction_monomials<k>()>::value();
    }

    template <int k>
    int256 initial_mult(int128 a_, int128 b_) {
        __m128i a = a_.reveal();
        __m128i b = b_.reveal();
        __m128i x0 = _mm_clmulepi64_si128(a, b, 0x00);
        __m128i x1 = _mm_setzero_si128();
        if constexpr(k > 64) { // Only do the other parts if we need them
            // (a0 + X*a1) * (b0 + X*b1) = a0*b0 + X*(a0*b1 + a1*b0) + X^2*(a1*b1)
            __m128i t10 = _mm_clmulepi64_si128(a, b, 0x10);
            __m128i t01 = _mm_clmulepi64_si128(a, b, 0x01);
            __m128i t11 = _mm_clmulepi64_si128(a, b, 0x11);
            __m128i middle = t01 ^ t10;
            x0 ^= _mm_slli_si128(middle, 8); // Shift is by bytes, apparently
            x1 = t11 ^ _mm_srli_si128(middle, 8);

        }
        return int256(x0, x1);
    }

    template <int k, typename T>
    T reduce_once(const T& x, int red) { // Trinomial
        T hi = x >> k;
        return (x & make_mask<T, k>()) ^ hi ^ (hi << red);
    }

    template <int k, typename T>
    T reduce_once(const T& x, const std::tuple<int, int, int>& red) { // Pentanomial
        T hi = x >> k;
        return (x & make_mask<T, k>()) ^ hi ^ (hi << std::get<0>(red)) ^ (hi << std::get<1>(red)) ^ (hi << std::get<2>(red));
    }

    template <int k, typename T>
    T reduce(const int256& x) {
        constexpr auto red = reduction_polynomial<k>();
        if constexpr(k <= 32) { // Do everything in a smaller size
            std::uint64_t y = reduce_once<k, std::uint64_t>(x.low().low(), red);
            return reduce_once<k, std::uint64_t>(y, red);
        } else if constexpr(k <= 64) { // Can ignore x1
            int128 y = reduce_once<k, int128>(x.low(), red);
            return reduce_once<k, int128>(y, red).low();
        } else {
            int256 y = reduce_once<k, int256>(x, red);
            return reduce_once<k, int256>(y, red).low();
        }
    }

    template <typename T>
    T random(PRNG& gen) {
        T res;
        gen.get_random_bytes(reinterpret_cast<uint8_t*>(&res), sizeof(T));
        return res;
    }

    // Let's do this with two int64s to make sure we don't violate any weird storage constraints
    template <>
    inline int128 random<int128>(PRNG& gen) {
        return int128(random<long long>(gen), random<long long>(gen));
    }

    template <int k, const typename datatype<detail::type_idx<k>()>::type table[(1<<k) + 1][1<<k], typename Self>
    class SmallGF2k {
        public:
            using F = typename datatype<detail::type_idx<k>()>::type;
            using Base = SmallGF2k<k, table, Self>;
        private:
            static inline F MASK = detail::make_mask<F, k>();

            explicit SmallGF2k<k, table, Self>(F f, bool /* skip mask */) : m_val(std::move(f)) {}
            friend Self;

        public:

            SmallGF2k<k, table, Self>(const std::int64_t& el) : m_val(F(el) & MASK) {}
            SmallGF2k<k, table, Self>() : m_val(0) {}

            static Self random(PRNG& gen) {
                return Self(detail::random<F>(gen));
            }

            Self operator+(const Base& other) const {
                return Self(m_val ^ other.m_val, true);
            }
            Base& operator+=(const Base& other) {
                return *this = (*this) + other;
            }

            Self operator-(const Base& other) const {
                return Self(m_val ^ other.m_val, true);
            }
            Base& operator-=(const Base& other) {
                return *this = (*this) - other;
            }

            Self operator*(const Base& other) const {
                return Self(table[m_val][other.m_val], true);
            }
            Base& operator*=(const Base& other) {
                return *this = (*this) * other;
            }

            Self inv() const {
                return Self(table[1<<k][m_val], true);
            }

            bool operator==(const Base& other) const {
                return m_val == other.m_val;
            }

            bool operator!=(const Base& other) const {
                return m_val != other.m_val;
            }

            std::array<bool, k> to_bits() const {
                std::array<bool, k> res;
                F a = m_val;
                for (int i = 0; i < k; i++) {
                    res[i] = a & 1;
                    a >>= 1;
                }
                return res;
            }

            static Self from_bits(const std::array<bool, k>& bits) {
                F a = 0;
                for (int i = 0; i < k; i++) {
                    a |= F(bits[i]) << i;
                }
                return Self(a, true);
            }

            /**
             * To be used only when needing access to the underlying bits, really
             */
            F force_int() const {
                return m_val;
            }

        private:
            F m_val;
    };
} //namespace detail

template <int k>
class GF2k {
    public:
        using F = typename detail::datatype<detail::type_idx<k>()>::type;
    private:
        static_assert(2 <= k && k <= 128, "Unsupported extension field");
        static inline F MASK = detail::make_mask<F, k>();

        explicit GF2k<k>(F f, bool /*skip mask*/) : m_val(std::move(f)) {}

    public:
        template <typename T>
        explicit GF2k<k>(const T& el) : m_val(F(el) & MASK) {}
        GF2k<k>() : m_val(0) {}

        static GF2k<k> random(PRNG& gen) {
            return GF2k<k>(detail::random<F>(gen)); // auto masked
        }

        GF2k<k> operator+(const GF2k<k>& other) const {
            return GF2k<k>(m_val ^ other.m_val, true);
        }
        GF2k<k>& operator+=(const GF2k<k>& other) {
            return *this = (*this) + other;
        }

        GF2k<k> operator-(const GF2k<k>& other) const {
            return GF2k<k>(m_val ^ other.m_val, true);
        }
        GF2k<k>& operator-=(const GF2k<k>& other) {
            return *this = (*this) - other;
        }

        GF2k<k> operator*(const GF2k<k>& other) const {
            F a = m_val;
            F b = other.m_val;
            return GF2k<k>(detail::extract<F>(detail::reduce<k, F>(detail::initial_mult<k>(a, b))));
        }
        GF2k<k>& operator*=(const GF2k<k>& other) {
            return *this = (*this) * other;
        }

        GF2k<k> inv() const {
            // Probably not the optimal way to compute multiplicative inverses
            // Ignores the case x = 0
            // x^(2^K - 2) = x^-1
            // 2^K - 2 = 0b11...10 => x^(2^K - 2) = (x^2)^(0b11...1)
            GF2k<k> res(1);
            GF2k<k> x = (*this) * (*this);
            for (int i = 0; i < k - 1; i++) {
                res *= x;
                x *= x;
            }
            return res;
        }

        bool operator==(const GF2k<k>& other) const {
            return m_val == other.m_val;
        }

        bool operator!=(const GF2k<k>& other) const {
            return m_val != other.m_val;
        }

        std::array<bool, k> to_bits() const {
            std::array<bool, k> res;
            std::int64_t a;
            if constexpr(k > 64) {
                a = m_val.low();
            } else {
                a = m_val;
            }
            for (int i = 0; i < std::min(64, k); i++) {
                res[i] = a & 1;
                a >>= 1;
            }
            if constexpr(k > 64) {
                a = m_val.high();
                for (int i = 0; i < k - 64; i++) {
                    res[i + 64] = a & 1;
                    a >>= 1;
                }
            }
            return res;
        }

        static GF2k<k> from_bits(const std::array<bool, k>& bits) {
            std::int64_t a = 0;
            for (int i = 0; i < std::min(64, k); i++) {
                a |= std::int64_t(bits[i]) << i;
            }
            if constexpr(k <= 64) {
                return GF2k<k>(F(a), true);
            } else {
                std::int64_t b = 0;
                for (int i = 64; i < k; i++) {
                    b |= std::int64_t(bits[i]) << (i - 64);
                }
                return GF2k<k>(F(a, b), true);
            }
        }

        /**
         * To be used only when needing access to the underlying bits, really
         */
        F force_int() const {
            return m_val;
        }

    private:
        F m_val;
};

#define SMALLFIELD(k) \
    template <> \
    class GF2k< k > : public detail::SmallGF2k< k, gftables::mul##k, GF2k< k >> { \
        using detail::SmallGF2k< k, gftables::mul##k, GF2k< k >>::SmallGF2k; \
    }

SMALLFIELD(2);
SMALLFIELD(3);
SMALLFIELD(4);
SMALLFIELD(5);
SMALLFIELD(6);
SMALLFIELD(7);
SMALLFIELD(8);

#undef SMALLFIELD


// Include here to have all GF2k<k> defined already and avoid circularity
#include "gflifttables.h"

template <int k2, int k>
GF2k<k2> liftGF(const GF2k<k>& base) {
    static_assert(k2 % k == 0, "No subfield of correct size exists");
    auto b = base.force_int();
    GF2k<k2> res(b & 1);
    for (int i = 1; i < k; i++) {
        b >>= 1;
        if (b & 1) res += gflifttables::lift_v<k, k2>[i];
    }
    return res;
}

