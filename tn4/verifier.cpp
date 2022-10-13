/*
Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/
#include "config.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <fstream>
#include <sstream>
#include <string>

#include "arith.h"
#include "Circuit.h"
#include "decoder.h"
#include "io.h"
#include "player.h"
#include "random.h"
#include "Timer.h"
#include "util.h"

using namespace std::literals::string_literals;

std::vector<ShareEl> compute_combinations(Player& me, const Circuit& circ, GFReader<K>& proof, GFReader<K>& preprocessing) {
    PRNG gen;
    gen.ReSeed(me.player_idx);
    me.commit_open_seed(gen, 0);

    std::vector<ShareEl> wires;
    for (size_t i = 0; i < circ.num_inputs(); i++) {
        for (size_t j = 0; j < circ.num_iWires(i); j++) {
            ShareEl mask = preprocessing.next();
            ShareEl diff = proof.next();
            wires.emplace_back(mask - diff);
        }
    }

    std::vector<ShareEl> A(REPETITIONS, ShareEl(0)), C(REPETITIONS, ShareEl(0));
    ShareEl circ_out = circ.eval_custom(wires,
            [](const ShareEl& a, const ShareEl& b) -> ShareEl {return a + b;},
            [&](const ShareEl& a, const ShareEl& b) -> ShareEl {
                ShareEl mask = preprocessing.next();
                ShareEl diff = proof.next();
                ShareEl c = mask - diff;
                for (int j = 0; j < REPETITIONS; j++) {
                    ShareEl beta = ShareEl::random(gen);
                    A[j] += beta * a * b;
                    C[j] += beta * c;
                }
                return c;
            },
            [](const ShareEl& a) -> ShareEl {return a + ShareEl(1);}
            );
    assert(circ.num_outputs() == 1);
    assert(circ.num_oWires(0) == 1);

    std::vector<ShareEl> res;
    res.push_back(circ_out);
    for (int j = 0; j < REPETITIONS; j++)
        res.push_back(A[j] - C[j]);

    return res;
}

bool validate(Player& me, const std::vector<ShareEl>& my_shares) {
    auto shares_to_send_raw = std::make_shared<BufferBitWriter>();
    GFWriter<K> shares_to_send(shares_to_send_raw);
    for (ShareEl el : my_shares) {
        shares_to_send.next(el);
    }
    me.send_all(shares_to_send_raw->drain(), 0);

    std::vector<Data> all_shares_raw = me.recv_from_all(0);
    std::vector<std::array<ShareEl, N>> all_shares(1 + REPETITIONS);
    for (int p = 1; p <= N; p++) {
        if (p == me.player_idx) {
            for (int i = 0; i < 1 + REPETITIONS; i++) {
                all_shares[i][p - 1] = my_shares[i];
            }
        } else {
            GFReader<K> reader(std::make_shared<BufferBitReader>(std::move(all_shares_raw[p])));
            for (int i = 0; i < 1 + REPETITIONS; i++) {
                all_shares[i][p - 1] = reader.next();
            }
        }
    }

    auto [circ_out, circ_out_cheaters] = decode<T, T>(all_shares[0]);
    complain_cheaters(circ_out_cheaters, "output reconstruction");

    if (circ_out[0] != ShareEl(0)) {
        std::cout << "Circuit output wasn't 0, invalid proof" << std::endl;
        return false;
    }

    for (int j = 0; j < REPETITIONS; j++) {
        auto [AmC, AmC_cheaters] = decode<2*T, T>(all_shares[j + 1]);
        complain_cheaters(AmC_cheaters, "reconstruction of (A - C)");
        if (AmC[0] != ShareEl(0)) {
            std::cout << "Multiplications are inconsistent; invalid proof" << std::endl;
            return false;
        }
    }

    return true;
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <network_config> <player_number> <circuit>" << std::endl;
        return 0;
    }

    std::istringstream pnum_stream(argv[2]);
    int player_num = -1;
    pnum_stream >> player_num;
    if (player_num <= 0) {
        std::cerr << "Invalid number" << std::endl;
        return 1;
    }

    std::ifstream circ_file(argv[3]);
    Circuit circ;
    circ_file >> circ;
    circ.sort();

    Data proof_raw;
    Player::run_protocol<N_TIMING_RUNS, PERFORM_TIMING>(argv[1], player_num, N,
            [&](Player& me) {
                // Only perform timing after receiving the proof to avoid double counting
                // prover time for verifiers
                proof_raw = me.recv_from(0);
            },
            
            [&](Player& me) {
                GFReader<K> preprocessing("Player" + std::to_string(me.player_idx) + ".pre");
                GFReader<K> proof(std::make_shared<BufferBitReader>(std::move(proof_raw)));
                std::vector<ShareEl> to_check = compute_combinations(me, circ, proof, preprocessing);
                return validate(me, to_check);
            },

            [](bool success, double time_taken, int nruns) {
                std::cout << "Proof(s) " << (success ? "accepted" : "rejected") << ".\n";
                std::cout << "Performed " << nruns << " verifier executions in (median) " << time_taken << " seconds." << std::endl;
            });
}
