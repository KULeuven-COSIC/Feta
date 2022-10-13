/*
Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/
#include "config.h"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>

#include "arith.h"
#include "Circuit.h"
#include "decoder.h"
#include "io.h"
#include "player.h"
#include "Timer.h"

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <network_config> <circuit> <private_input> <batch_size>" << std::endl;
        return 0;
    }

    std::ifstream circ_file(argv[2]);
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

    int proof_size = -1;

    Player::run_protocol<N_TIMING_RUNS, PERFORM_TIMING>(argv[1], 0, N,
            [](Player& me) { }, // Nothing special for setup

            [&](Player& me) { // The protocol
                FileBitReader private_input(argv[3]);
                GFReader<K> preprocessing("Player0.pre");

                auto output_writer = std::make_shared<BufferBitWriter>();
                auto output = GFWriter<K>(output_writer);

                std::vector<bool> wires;
                for (size_t i = 0; i < circ.num_inputs(); i++) {
                    for (size_t j = 0; j < circ.num_iWires(i); j++) {
                        bool inp = private_input.getbit();
                        ShareEl mask = preprocessing.next();
                        output.next(mask - static_cast<ShareEl>(inp));
                        wires.push_back(inp);
                    }
                }

                vector<ShareEl> A, B;
                bool res = circ.eval_custom(wires,
                        [](bool a, bool b) -> bool {return a ^ b;},
                        [&A, &B, &preprocessing, &output](bool a, bool b) -> bool {
                            ShareEl mask = preprocessing.next();
                            output.next(mask - ShareEl(a & b));
                            A.emplace_back(a);
                            B.emplace_back(b);
                            return a && b;
                        },
                        [](bool a) -> bool {return !a;}
                        );
                assert(circ.num_outputs() == 1);
                assert(circ.num_oWires(0) == 1);
                assert(res == 0);

                int n1 = (A.size() + n2 - 1) / n2; // Rounding up
                A.resize(n1 * n2, ShareEl(0)); // Extend the capacity with zeroes
                B.resize(n1 * n2, ShareEl(0));

                // if using ρ full repetitions and σ SZ values, we need to add ρσ extra points for every interpolation
                std::vector<ShareEl> ts(2 * n1 * FULL_REPETITIONS * SZ_REPETITIONS);
                PRNG gen;
                gen.ReSeed(0);
                for (auto& t : ts) {
                    t = ShareEl::random(gen);
                    ShareEl mask = preprocessing.next();
                    output.next(mask - t);
                }

                Data output_to_hash = output_writer->drain();
                Data seed = Hash(output_to_hash);
                gen.SetSeedFromRandom(seed.data());
                std::vector<ShareEl> rs(n1 * FULL_REPETITIONS);
                for (auto& r : rs) {
                    r = ShareEl::random(gen);
                }

                std::vector<std::vector<ShareEl>> interpolation_preprocessing;
                for (int i = 0; i < n2 + 2 * SZ_REPETITIONS; i++) {
                    interpolation_preprocessing.emplace_back(interpolate_preprocess(n2 + SZ_REPETITIONS, ShareEl(n2 + i)));
                }
                for (int full = 0; full < FULL_REPETITIONS; full++) {
                    std::vector<ShareEl> ps(n2 + 2 * SZ_REPETITIONS, ShareEl(0));
                    for (int j = 0; j < n1; j++) {
                        std::vector<ShareEl> ptsA(A.begin() + j * n2, A.begin() + (j + 1) * n2);
                        for (auto& a : ptsA) {
                            a *= rs[full * n1 + j];
                        }
                        for (int k = 0; k < SZ_REPETITIONS; k++) {
                            ptsA.push_back(ts[full * 2 * n1 * SZ_REPETITIONS + j * 2 * SZ_REPETITIONS + k]);
                        }
                        std::vector<ShareEl> ptsB(B.begin() + j * n2, B.begin() + (j + 1) * n2);
                        for (int k = 0; k < SZ_REPETITIONS; k++) {
                            ptsB.push_back(ts[full * 2 * n1 * SZ_REPETITIONS + (2*j + 1) * SZ_REPETITIONS + k]);
                        }
                        for (int i = 0; i < n2 + 2 * SZ_REPETITIONS; i++) {
                            ps[i] += interpolate_with_preprocessing(interpolation_preprocessing[i], ptsA) * interpolate_with_preprocessing(interpolation_preprocessing[i], ptsB);
                        }
                    }
                    for (const auto& p : ps) {
                        output.next(preprocessing.next() - p);
                    }
                }

                me.send_all(output_to_hash); // No deadlock, not receiving
                Data part_2 = output_writer->drain();
                me.send_all(part_2); // No deadlock, not receiving
                proof_size = output_to_hash.size() + part_2.size();

                return res == 0;
            },

            [&](bool /* success */, double time_taken, int nruns) { // Reporting
                std::cout << "Proof size: " << proof_size << " bytes.\n";
                std::cout << "Performed " << nruns << " prover execution(s) in (median) " << time_taken << " seconds." << std::endl;
            });
}
