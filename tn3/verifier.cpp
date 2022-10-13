/*
Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/
#include "config.h"

#include <array>
#include <algorithm>
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

ShareEl eval_circuit(const Circuit& circ,
        GFReader<K>& preprocessing,
        GFReader<K>& proof,
        std::vector<ShareEl>& A,
        std::vector<ShareEl>& B,
        std::vector<ShareEl>& C) {
    std::vector<ShareEl> wires;
    for (size_t i = 0; i < circ.num_inputs(); i++) {
        for (size_t j = 0; j < circ.num_iWires(i); j++) {
            ShareEl mask = preprocessing.next();
            ShareEl diff = proof.next();
            wires.emplace_back(mask - diff);
        }
    }

    return circ.eval_custom(wires,
            [](const ShareEl& a, const ShareEl& b) -> ShareEl {return a + b;},
            [&](const ShareEl& a, const ShareEl& b) -> ShareEl {
                ShareEl mask = preprocessing.next();
                ShareEl diff = proof.next();
                ShareEl c = mask - diff;
                A.push_back(a);
                B.push_back(b);
                C.push_back(c);
                return c;
            },
            [](const ShareEl& v) -> ShareEl {return v + ShareEl(1);}
        );
}

std::vector<ShareEl> get_P(const std::vector<ShareEl>& C, const std::vector<ShareEl>& rs, GFReader<K>& proof_ps, GFReader<K>& preprocessing, int n1, int n2, int full) {
    std::vector<ShareEl> ps(2 * n2 + 2 * SZ_REPETITIONS, ShareEl(0));
    for (std::size_t i = 0; i < C.size(); i++) {
        ps[i % n2] += rs[full * n1 + (i / n2)] * C[i];
    }
    for (int i = 0; i < n2 + 2 * SZ_REPETITIONS; i++) {
        ps[n2 + i] = preprocessing.next() - proof_ps.next();
    }
    return ps;
}

std::vector<ShareEl> verification(
        const std::vector<ShareEl>& A,
        const std::vector<ShareEl>& B,
        const std::vector<ShareEl>& ps,
        const std::vector<ShareEl>& rs,
        const std::vector<ShareEl>& ts,
        int n1, int n2,
        int full,
        ShareEl zeta
        ) {
    std::vector<ShareEl> pre = interpolate_preprocess(n2 + SZ_REPETITIONS, zeta);
    ShareEl P = interpolate(ps, zeta);
    std::vector<ShareEl> res;
    res.reserve(1 + 2 * n1);
    res.push_back(P);
    for (int j = 0; j < n1; j++) {
        std::vector<ShareEl> ptsA(A.begin() + j * n2, A.begin() + (j + 1) * n2);
        for (auto& a: ptsA) {
            a *= rs[full * n1 + j];
        }
        for (int k = 0; k < SZ_REPETITIONS; k++) {
            ptsA.push_back(ts[full * 2 * n1 * SZ_REPETITIONS + j * 2 * SZ_REPETITIONS + k]);
        }

        std::vector<ShareEl> ptsB(B.begin() + j * n2, B.begin() + (j + 1) * n2);
        for (int k = 0; k < SZ_REPETITIONS; k++) {
            ptsB.push_back(ts[full * 2 * n1 * SZ_REPETITIONS + (2*j + 1) * SZ_REPETITIONS + k]);
        }

        res.push_back(interpolate_with_preprocessing(pre, ptsA));
        res.push_back(interpolate_with_preprocessing(pre, ptsB));
    }
    return res;
}

bool open_all_and_check(std::vector<Data>&& all_shares_raw, int n1) {
    std::vector<GFReader<K>> all_shares;
    for (int i = 1; i <= N; i++) {
        all_shares.emplace_back(std::make_shared<BufferBitReader>(std::move(all_shares_raw[i])));
    }
    std::array<ShareEl, N> shares;
    auto populate = [&shares, &all_shares]() { for (int j = 0; j < N; j++) shares[j] = all_shares[j].next(); };

    populate();
    auto [outwire_val, outwire_cheaters] = decode<T, T>(shares);
    complain_cheaters(outwire_cheaters, "Opening of output wire o");

    bool okay = outwire_val[0] == ShareEl{0};

    for (int i = 0; i < FULL_REPETITIONS * SZ_REPETITIONS; i++) {
        std::array<ShareEl, N> shares;
        populate();
        auto [P_val, P_cheaters] = decode<T, T>(shares);
        complain_cheaters(P_cheaters, "Opening of P");
        ShareEl AB_verif{0};
        for (int j = 0; j < n1; j++) {
            populate();
            auto [A_val, A_cheaters] = decode<T, T>(shares);
            complain_cheaters(A_cheaters, "Opening of an A(zeta)");
            populate();
            auto [B_val, B_cheaters] = decode<T, T>(shares);
            complain_cheaters(B_cheaters, "Opening of a B(zeta)");
            AB_verif += A_val[0] * B_val[0];
        }
        okay = okay && (P_val[0] == AB_verif);
    }
    return okay;
}

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <network_config> <player_number> <circuit> <batch_size>" << std::endl;
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

    std::istringstream batchsize_reader(argv[4]);
    int n2 = -1;
    batchsize_reader >> n2;
    if (n2 <= 0) {
        std::cerr << "Invalid batch size (n2)" << std::endl;
        return 1;
    }

    Data proof_raw_1;
    Player::run_protocol<N_TIMING_RUNS, PERFORM_TIMING>(argv[1], player_num, N, 
            [&](Player& me) {
                // Only perform timing after receiving this part of the proof to avoid double counting
                // prover time for verifiers
                proof_raw_1 = me.recv_from(0);
            },

            [&](Player& me) {
                GFReader<K> preprocessing("Player" + std::to_string(me.player_idx) + ".pre");
                Data proof_raw_2 = me.recv_from(0);
                PRNG gen;
                gen.ReSeed(me.player_idx);
                me.commit_open_seed(gen, 0);
                std::array<ShareEl, FULL_REPETITIONS * SZ_REPETITIONS> zetas;
                for (auto& zeta : zetas) {
                    do {
                        zeta = ShareEl::random(gen);
                    } while (zeta.force_int() < (unsigned)n2); // ZK requirement
                }

                Data seed = Hash(proof_raw_1);
                GFReader<K> proof_1(std::make_shared<BufferBitReader>(std::move(proof_raw_1)));
                GFReader<K> proof_2(std::make_shared<BufferBitReader>(std::move(proof_raw_2)));

                std::vector<ShareEl> A, B, C;
                ShareEl o_share = eval_circuit(circ, preprocessing, proof_1, A, B, C);
                int n1 = (A.size() + n2 - 1) / n2; // Rounding up
                A.resize(n1 * n2, ShareEl(0));
                B.resize(n1 * n2, ShareEl(0));
                C.resize(n1 * n2, ShareEl(0));

                std::vector<ShareEl> ts(2 * n1 * FULL_REPETITIONS * SZ_REPETITIONS);
                for (auto& t : ts) {
                    t = preprocessing.next() - proof_1.next();
                }

                gen.SetSeedFromRandom(seed.data());
                std::vector<ShareEl> rs(n1 * FULL_REPETITIONS);
                for (auto& r : rs) {
                    r = ShareEl::random(gen);
                }

                auto to_open_writer = std::make_shared<BufferBitWriter>();
                auto to_open = GFWriter<K>(to_open_writer);
                to_open.next(o_share);

                for (int full = 0; full < FULL_REPETITIONS; full++) {
                    std::vector<ShareEl> ps = get_P(C, rs, proof_2, preprocessing, n1, n2, full);
                    for (int z = 0; z < SZ_REPETITIONS; z++) {
                        for (ShareEl pt : verification(A, B, ps, rs, ts, n1, n2, full, zetas[full * SZ_REPETITIONS + z])) {
                            to_open.next(pt);
                        }
                    }
                }
                Data my_shares = to_open_writer->drain();
                me.send_all(my_shares, 0);
                std::vector<Data> all_shares = me.recv_from_all(0); // `FULL_REPETITIONS * SZ_REPETITIONS * (2*n1) * K` should be small enough to avoid deadlocks for now; TODO
                all_shares[me.player_idx] = std::move(my_shares);
                return open_all_and_check(std::move(all_shares), n1);
            },

            [](bool success, double time_taken, int nruns) {
                std::cout << "Proof(s) " << (success ? "accepted" : "rejected") << ".\n";
                std::cout << "Performed " << nruns << " verifier executions in (median) " << time_taken << " seconds." << std::endl;
            });
}
