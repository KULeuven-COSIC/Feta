/*
Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/
#include "config.h"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>

#include "arith.h"
#include "Circuit.h"
#include "io.h"
#include "player.h"
#include "Timer.h"

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <network_config> <circuit> <private_input>" << std::endl;
        return 0;
    }

    std::ifstream circ_file(argv[2]);
    Circuit circ;
    circ_file >> circ;
    circ.sort();

    int proof_size = -1;

    Player::run_protocol<N_TIMING_RUNS, PERFORM_TIMING>(argv[1], 0, N,
            [](Player& me) {  }, // Nothing special for setup
            
            [&](Player& me) { // run
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

                bool res = circ.eval_custom(wires,
                        [](bool a, bool b) -> bool {return a ^ b;},
                        [&preprocessing, &output](bool a, bool b) -> bool {
                            ShareEl mask = preprocessing.next();
                            output.next(mask - ShareEl(a & b));
                            return a && b;
                        },
                        [](bool a) -> bool {return !a;}
                        );
                assert(circ.num_outputs() == 1);
                assert(circ.num_oWires(0) == 1);
                assert(res == 0);

                Data proof = output_writer->drain();
                me.send_all(proof); // No deadlock, not receiving
                proof_size = proof.size();

                return res == 0;
            },

            [&](bool /* success */, double time_taken, int nruns) { // Reporting
                std::cout << "Proof size: " << proof_size << " bytes.\n";
                std::cout << "Performed " << nruns << " prover execution(s) in (median) " << time_taken << " seconds." << std::endl;
            });
}
