/*
Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/
#pragma once

#include <algorithm>
#include <array>
#include <fstream>
#include <istream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "networking.h"
#include "random.h"
#include "Timer.h"

class invalid_signature : public std::runtime_error {
    public:
        invalid_signature(int player) : std::runtime_error((std::string("Invalid signature from player ") + std::to_string(player)).c_str()) {}
};

/**
 * Represent a player, providing communication to other players and high-level
 * routines to perform the protocols.
 * The driver programs still control the actual flow of the protocol, so this can be re-used.
 * By convention, the prover is player 0.
 */
class Player {
    public:
        Player(int idx, std::istream& network_config, int N);

        void close_connection(int peer);

        // Template instantiations done in the implementation
        template <bool sign=false>
        Data recv_from(int player);

        template <bool sign=false>
        std::vector<Data> recv_from_all(int skip=-1);

        template <bool sign=false>
        void send_to(int player, const Data& data);

        template <bool sign=false>
        void send_all(const Data& data, int skip=-1);

        void commit_open_seed(PRNG& gen, int skip=-1);

        void sync();

        int player_idx;

        template <int NumTimes, bool DoTime, typename Setup, typename Run, typename Report>
        static void run_protocol(const std::string& netconfig, int idx, int N, const Setup& setup, const Run& run, const Report& report);

    private:
        void send_to_with_sig(int player, const Data& data, const Data& sig);

        int N;
        NetworkInfo m_network;
};


template <int NumTimes, bool DoTime, typename Setup, typename Run, typename Report>
void Player::run_protocol(const std::string& netconfig, int idx, int N, const Setup& setup, const Run& run, const Report& report) {
    std::ifstream netconfig_file(netconfig);
    Player me(idx, netconfig_file, N);

    bool res = true;
    if constexpr(DoTime) {
        Timer t;
        std::array<double, NumTimes> timings;
        for (int i = 0; i < NumTimes; i++) {
            me.sync();
            setup(me);
            t.start();
            res = res && run(me);
            t.stop();
            timings[i] = t.elapsed();
            t.reset();
        }
        std::sort(timings.begin(), timings.end());
        report(res, timings[NumTimes/2], NumTimes);
    } else {
        Timer t;
        setup(me);
        t.start();
        res = run(me);
        t.stop();
        report(res, t.elapsed(), 1);
    }
}
