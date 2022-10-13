/*
Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/
#if !defined(CONFIG_FILE)
#error No config.h specified, please use -DCONFIG_FILE=directory to specify the directoy in which to look for config.h
#else
#define stringify(x) #x
#define concat(x, y) stringify(x/y)
#include concat(CONFIG_FILE, config.h)
#undef concat
#undef stringify
#endif

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <vector>
using namespace std::string_literals;

#include "decoder.h"
#include "io.h"
#include "player.h"
#include "random.h"
#include "Timer.h"

template <int k, typename F>
std::vector<std::vector<GF2k<k>>> sample_shares(Player& me, PRNG& gen, int num_samples, F coord_for) {
    using El = GF2k<k>;
    std::vector<std::vector<El>> secrets(num_samples, std::vector<El>(N, El(0)));

    auto do_receive = [&](int p) { // Some re-occuring code, easier to just capture than pass stuff around
        auto reader = std::make_shared<BufferBitReader>(me.recv_from<true>(p));
        auto gfreader = GFReader<k>(std::move(reader));
        for (int i = 0; i < num_samples; i++) {
            secrets[i][p - 1] = gfreader.next();
        }
    };

    if (me.player_idx != 0) { // Make shares and send them
        std::vector<std::shared_ptr<BufferBitWriter>> output_queues;
        std::vector<GFWriter<k>> output_writers;
        for (int i = 0; i <= N; i++) {
            output_queues.emplace_back(std::make_shared<BufferBitWriter>());
            output_writers.emplace_back(output_queues.back());
        }

        std::array<El, N> xcoords;
        for (int i = 0; i < N; i++) xcoords[i] = coord_for(i);

        for (int i = 0; i < num_samples; i++) {
            // generate a random polynomial, distribute secret to Player0, shares to other players
            std::array<El, T + 1> poly;

            for (int j = 0; j <= T; j++) {
                poly[j] = El::random(gen);
            }

            output_writers[0].next(poly[0]);

            std::array<El, N> shares = encode<T, k, N>(xcoords, poly);
            for (int p = 1; p <= N; p++) {
                if (p == me.player_idx) {
                    secrets[i][p - 1] = shares[p - 1];
                } else {
                    output_writers[p].next(shares[p - 1]);
                }
            }
        }

        // Fun with avoiding deadlocks, it's not a regular all-around broadcast, so can't use a helper in Player
        // send to lower, receive from higher, send to higher, receiver from lower
        for (int p = 0; p < me.player_idx; p++) {
            me.send_to<true>(p, output_queues[p]->drain());
        }
        for (int p = me.player_idx + 1; p <= N; p++) {
            do_receive(p);
        }
        // This one must be in reverse order, to prevent another deadlock cycle
        for (int p = N; p > me.player_idx; p--) {
            me.send_to<true>(p, output_queues[p]->drain());
        }
        // DON'T receive from the prover...
        for (int p = 1; p < me.player_idx; p++) {
            do_receive(p);
        }
    } else {
        // Only need to receive
        for (int p = 1; p <= N; p++) {
            do_receive(p);
        }
    }

    return secrets;
}

template <int k, typename F>
bool check_linear_combinations(Player& me, PRNG& gen, const std::vector<std::vector<GF2k<k>>>& secrets, int SECRETS_TO_SAMPLE, F coord_for) {
    using El = GF2k<k>;
    me.commit_open_seed(gen);

    auto lincombs_writer = std::make_shared<BufferBitWriter>();
    GFWriter<k> lincombs(lincombs_writer);
    for (int r = 0; r < PREPROCESSING_REPETITIONS; r++) {
        El comb(0);
        for (int i = 0; i < SECRETS_TO_SAMPLE; i++) {
            for (int j = 1; j <= N; j++) {
                auto coeff = El::random(gen);
                comb += coeff * secrets[i][j - 1];
            }
        }
        lincombs.next(comb);
    }

    // Send share openings for the linear combination/send the actual linear combination
    Data my_combinations = lincombs_writer->drain();
    me.send_all(my_combinations); // Should be safe from deadlocks; `PREPROCESSING_REPETITIONS * K` is small

    Data expected_raw;
    std::vector<Data> shares_raw = me.recv_from_all(0);
    if (me.player_idx == 0) {
        expected_raw = std::move(my_combinations);
    } else { // Verifier
        expected_raw = me.recv_from(0);
        shares_raw[me.player_idx] = std::move(my_combinations);
    }

    std::vector<GFReader<k>> shares;
    for (int p = 1; p <= N; p++) {
        shares.emplace_back(std::make_shared<BufferBitReader>(std::move(shares_raw[p])));
    }

    GFReader<k> expected_reader(std::make_shared<BufferBitReader>(std::move(expected_raw)));
    std::vector<El> expected;

    std::array<El, N> xcoords;
    for (int i = 0; i < N; i++) xcoords[i] = coord_for(i);

    // Open all these PREPROCESSING_REPETITIONS sharings
    std::vector<El> all_opened;

    std::vector<El> interp_xcoords;
    for (int i = 0; i < T + 1; i++) interp_xcoords.push_back(xcoords[i]);
    std::vector<std::vector<El>> interp_pre;
    for (int i = T + 1; i < N; i++) {
        interp_pre.emplace_back(interpolate_preprocess(interp_xcoords, xcoords[i]));
    }
    interp_pre.emplace_back(interpolate_preprocess(interp_xcoords, El{0}));

    for (int i = 0; i < PREPROCESSING_REPETITIONS; i++) {
        std::vector<El> d(T + 1);
        for (int p = 0; p < T + 1; p++) {
            d[p] = shares[p].next();
        }
        for (int p = T + 1; p < N; p++) {
            auto to_check = shares[p].next();
            if (to_check != interpolate_with_preprocessing(interp_pre[p - T - 1], d)) {
                std::cerr << "Inconsistency detected in random linear combination sharing" << std::endl;
            }
        }
        auto opened = interpolate_with_preprocessing(interp_pre.back(), d);
        all_opened.push_back(opened);
        expected.push_back(expected_reader.next());
    }

    return all_opened == expected;
}

template <int k>
std::vector<GF2k<k>> compute_Vandermonde(const std::vector<std::vector<GF2k<k>>>& secrets, int num_samples) {
    using El = GF2k<k>;
    std::vector<El> res((N - T) * num_samples, El(0));
    for (int i = 0; i < num_samples; i++) {
        for (int j = 0; j < N - T; j++) {
            El coeff(j + 1);
            for (El s : secrets[i]) {
                res[i * (N - T) + j] += coeff * s;
                coeff *= El(j + 1);
            }
        }
    }
    return res;
}

int main(int argc, char** argv) {
#if defined(PREPROCESSING_SECOND_FIELD)
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <network_config> <player_number> <number_of_outputs_field_1> <number_of_outputs_field_2>" << std::endl;
        return 0;
    }
#else
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <network_config> <player_number> <number_of_outputs>" << std::endl;
        return 0;
    }
#endif

    int player_num = -1;
    int nout = -1;
    std::istringstream nout_tmp(std::string(argv[2]) + " " + argv[3]);
    nout_tmp >> player_num >> nout;
    if (nout <= 0 || player_num < 0) {
        std::cerr << "Invalid number" << std::endl;
        return 1;
    }
#if defined(PREPROCESSING_SECOND_FIELD)
    int noutC = -1;
    std::istringstream noutC_tmp{std::string(argv[4])};
    noutC_tmp >> noutC >> nout;
    if (noutC <= 0) {
        std::cerr << "Invalid number" << std::endl;
        return 1;
    }
#endif

    const int SECRETS_TO_SAMPLE = (nout + PREPROCESSING_REPETITIONS + (N - T - 1)) / (N - T); // Rounding up by flooring (n + d - 1) / d

    auto coord_for_base = [](int i) -> ShareEl { return ShareEl{i + 1}; };

    std::vector<ShareEl> final_output;
#if defined(PREPROCESSING_SECOND_FIELD)
    const int SECRETS_TO_SAMPLE_C = (noutC + PREPROCESSING_REPETITIONS_EXT + (N - T - 1)) / (N - T); // Rounding up by flooring (n + d - 1) / d
    std::vector<CheckEl> final_outputC;
    auto coord_for_ext = [](int i) -> CheckEl { return liftGF<K_EXT>(ShareEl{i + 1}); };
#endif
    Player::run_protocol<N_TIMING_RUNS, PERFORM_TIMING>(argv[1], player_num, N,
            [](Player& me) {  }, // No special setup

            [&](Player& me) { // run
                PRNG gen;
                gen.ReSeed(player_num);

                auto secrets = sample_shares<K>(me, gen, SECRETS_TO_SAMPLE, coord_for_base);
                if (!check_linear_combinations(me, gen, secrets, SECRETS_TO_SAMPLE, coord_for_base)) {
                    std::cerr << "Linear combinations are incorrect!" << std::endl;
                    return false;
                }
                final_output = compute_Vandermonde(secrets, SECRETS_TO_SAMPLE);
                final_output.resize(nout);

#if defined(PREPROCESSING_SECOND_FIELD)
                gen.ReSeed(player_num + N + 1);
                auto secretsC = sample_shares<K_EXT>(me, gen, SECRETS_TO_SAMPLE_C, coord_for_ext);
                if (!check_linear_combinations(me, gen, secretsC, SECRETS_TO_SAMPLE_C, coord_for_ext)) {
                    std::cerr << "Linear combinations are incorrect!" << std::endl;
                    return false;
                }
                final_outputC = compute_Vandermonde(secretsC, SECRETS_TO_SAMPLE_C);
                final_outputC.resize(noutC);
#endif

                return true;
            },

            [&](bool /* success */, double time_taken, int nruns) { // reporting
#if defined(PREPROCESSING_SECOND_FIELD)
                std::cout << "Performed " << nruns << " preprocessing executions (for " << nout << " + " << noutC << " field elements) in (median) " << time_taken << " seconds." << std::endl;
#else
                std::cout << "Performed " << nruns << " preprocessing executions (for " << nout << " field elements) in (median) " << time_taken << " seconds." << std::endl;
#endif
            });

    // Save to file
    auto outfile_writer = std::make_shared<FileBitWriter>("Player" + std::to_string(player_num) + ".pre");
    GFWriter<K> out(outfile_writer);
    for (ShareEl el : final_output) {
        out.next(el);
    }
#if defined(PREPROCESSING_SECOND_FIELD)
    GFWriter<K_EXT> outC(outfile_writer);
    for (CheckEl el : final_outputC) {
        outC.next(el);
    }
#endif
}
