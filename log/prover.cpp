/*
Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/
#include "config.h"

#include <fstream>
#include <iostream>
#include <utility>

#include "Circuit.h"
#include "decoder.h"
#include "io.h"
#include "player.h"

#include "common.cpp" // Very ugly, but allows for nice inlining and some shared code between prover and verifier

/**
 * Commit to the sum of product polynomials by emitting differences
 *  between the coefficients and `preprocessing` elements to `output`.
 *
 * Then the inner-product triple is compressed by performing a Schwartz-Zippel evaluation.
 */
std::tuple<CheckEl, std::vector<CheckEl>, std::vector<CheckEl>> commit_and_compress(
        const CheckEl& innerprod,
        const std::vector<CheckEl>& xs,
        const std::vector<CheckEl>& ys,
        GFReader<K_EXT>& preprocessing,
        GFWriter<K_EXT>& output,
        const std::shared_ptr<HashableBufferBitWriter>& outwriter) {

    int num_elem = xs.size();

    std::array<CheckEl, 2*COMPRESSION-1> product_poly{CheckEl{0}};
    int i;
    for (i = 0; i <= num_elem - COMPRESSION; i += COMPRESSION) {
        std::array<CheckEl, COMPRESSION> x_pts, y_pts;
        for (int j = 0; j < COMPRESSION; j++) x_pts[j] = xs[i + j];
        for (int j = 0; j < COMPRESSION; j++) y_pts[j] = ys[i + j];

        auto to_add = poly_mul(interpolate_poly(x_pts), interpolate_poly(y_pts));

        for (int j = 0; j < 2*COMPRESSION - 1; j++) {
            product_poly[j] += to_add[j];
        }
    }

    // If it's not evenly divisible; implicitly fill with zeroes
    if (i < num_elem) {
        std::array<CheckEl, COMPRESSION> x_pts, y_pts;
        int j;
        for (j = 0; i + j < num_elem; j++) {
            x_pts[j] = xs[i + j];
            y_pts[j] = ys[i + j];
        }
        for (; j < COMPRESSION; j++) {
            x_pts[j] = CheckEl{0};
            y_pts[j] = CheckEl{0};
        }

        auto to_add = poly_mul(interpolate_poly(x_pts), interpolate_poly(y_pts));

        for (int j = 0; j < 2*COMPRESSION - 1; j++) {
            product_poly[j] += to_add[j];
        }
    }

    // Commit to it
    for (int i = 0; i < 2*COMPRESSION - 2; i++) { // Commit to deg out of deg + 1 coefficients
        output.next(preprocessing.next() - product_poly[i]);
    }

    // Fiat-Shamir for the evaluation point during compression
    PRNG gen;
    outwriter->hash_seed(gen);
    CheckEl r = CheckEl::random(gen);

    // Do the compression
    CheckEl z{0};
    std::vector<CheckEl> newxs;
    std::vector<CheckEl> newys;
    auto preproc = interpolate_preprocess(COMPRESSION, r);
    for (i = 0; i <= num_elem - COMPRESSION; i += COMPRESSION) {
        newxs.push_back(interpolate_with_preprocessing(preproc, std::vector<CheckEl>(xs.begin() + i, xs.begin() + i + COMPRESSION)));
        newys.push_back(interpolate_with_preprocessing(preproc, std::vector<CheckEl>(ys.begin() + i, ys.begin() + i + COMPRESSION)));
        z += newxs.back() * newys.back();
    }
    if (i < num_elem) {
        std::vector<CheckEl> xpts(xs.begin() + i, xs.end());
        xpts.resize(COMPRESSION, CheckEl{0});
        std::vector<CheckEl> ypts(ys.begin() + i, ys.end());
        ypts.resize(COMPRESSION, CheckEl{0});

        newxs.push_back(interpolate_with_preprocessing(preproc, xpts));
        newys.push_back(interpolate_with_preprocessing(preproc, ypts));
        z += newxs.back() * newys.back();
    }

    assert(z == poly_eval(product_poly, r));
    return {z, newxs, newys};
}

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
            [](Player& me) {  }, // No special setup

            [&](Player& me) { // Prove it
                FileBitReader private_input(argv[3]);
                auto preprocessing_reader = std::make_shared<FileBitReader>("Player0.pre");
                GFReader<K> preprocessing(preprocessing_reader);
                GFReader<K_EXT> preprocessingC(preprocessing_reader);

                auto output_writer = std::make_shared<HashableBufferBitWriter>();
                auto output = GFWriter<K>(output_writer);

                std::vector<bool> wires;
                for (size_t i = 0; i < circ.num_inputs(); i++) {
                    for (size_t j = 0; j < circ.num_iWires(i); j++) {
                        bool inp = private_input.getbit();
                        ShareEl mask = preprocessing.next();
                        output.next(mask - ShareEl{inp});
                        wires.push_back(inp);
                    }
                }

                vector<CheckEl> A, B, C;
                bool res = circ.eval_custom(wires,
                        [](bool a, bool b) -> bool {return a ^ b;},
                        [&](bool a, bool b) -> bool {
                            ShareEl mask = preprocessing.next();
                            output.next(mask - ShareEl(a && b));
                            A.emplace_back(a);
                            B.emplace_back(b);
                            C.emplace_back(a && b);
                            return a && b;
                        },
                        [](bool a) -> bool {return !a;}
                        );
                assert(circ.num_outputs() == 1);
                assert(circ.num_oWires(0) == 1);
                assert(res == 0);

                GFWriter<K_EXT> checkwriter(output_writer);
                // add random mult triple to ensure ZK when the final remaining mult is checked
                PRNG gen;
                gen.ReSeed(0);
                CheckEl a{CheckEl::random(gen)};
                CheckEl b{CheckEl::random(gen)};
                CheckEl c = a * b;
                checkwriter.next(preprocessingC.next() - a);
                checkwriter.next(preprocessingC.next() - b);
                checkwriter.next(preprocessingC.next() - c);
                A.push_back(a);
                B.push_back(b);
                C.push_back(c);

                // First Fiat-Shamir: randomizing the multiplication triples into an inner product triple
                output_writer->hash_seed(gen);
                CheckEl innerprod = randomize_to_inner_product(A, C, gen);
                while (A.size() > 1) {
                    std::tie(innerprod, A, B) = commit_and_compress(innerprod, A, B, preprocessingC, checkwriter, output_writer);
                }

                Data proof = output_writer->drain();
                proof_size = proof.size();
                me.send_all(proof); // No deadlock since we're not waiting to receive

                return res == 0;
            },

            [&](bool /* success */, double time_taken, int nruns) {
                std::cout << "Proof size: " << proof_size << " bytes.\n";
                std::cout << "Performed " << nruns << " prover execution(s) in (median) " << time_taken << " seconds." << std::endl;
            });
}
