/*
Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/
#include "config.h"

#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <tuple>

#include "arith.h"
#include "Circuit.h"
#include "decoder.h"
#include "io.h"
#include "player.h"
#include "util.h"

#include "common.cpp" // Very ugly, but allows for nice inlining and some shared code between prover and verifier

/**
 * Utility class to bundle a GFReader with the ability to take a hash for Fiat-Shamir at any point,
 *   as if only the elements that have been read so far were included in the hash.
 */
class FSProofStream {
    public:
        FSProofStream(std::shared_ptr<BufferBitReader>&& ptr) :
            m_proof(ptr),
            m_proofC(std::move(ptr)),
            m_consumed_writer(std::make_shared<HashableBufferBitWriter>()),
            m_consumed(m_consumed_writer),
            m_consumedC(m_consumed_writer) { }
        FSProofStream(Data&& proof_raw) : FSProofStream(std::make_shared<BufferBitReader>(std::move(proof_raw))) {  };

        ShareEl next() {
            ShareEl res = m_proof.next();
            m_consumed.next(res);
            return res;
        }

        CheckEl nextC() {
            CheckEl res = m_proofC.next();
            m_consumedC.next(res);
            return res;
        }

        void hash_seed(PRNG& gen) {
            m_consumed_writer->hash_seed(gen);
        }

    private:
        GFReader<K> m_proof;
        GFReader<K_EXT> m_proofC;
        std::shared_ptr<HashableBufferBitWriter> m_consumed_writer;
        GFWriter<K> m_consumed;
        GFWriter<K_EXT> m_consumedC;
};

CheckEl evaluate_circuit(const Circuit& circ, FSProofStream& proof, GFReader<K>& preprocessing,
        std::vector<CheckEl>& As, std::vector<CheckEl>& Bs, std::vector<CheckEl>& Cs) {
    std::vector<ShareEl> wires;
    for (size_t i = 0; i < circ.num_inputs(); i++) {
        for (size_t j = 0; j < circ.num_iWires(i); j++) {
            ShareEl mask = preprocessing.next();
            ShareEl diff = proof.next();
            wires.emplace_back(mask - diff);
        }
    }

    ShareEl circ_out = circ.eval_custom(wires,
            [](const ShareEl& a, const ShareEl& b) -> ShareEl {return a + b;},
            [&](const ShareEl& a, const ShareEl& b) -> ShareEl {
                ShareEl c = preprocessing.next() - proof.next();
                As.push_back(liftGF<K_EXT>(a));
                Bs.push_back(liftGF<K_EXT>(b));
                Cs.push_back(liftGF<K_EXT>(c));
                return c;
            },
            [](const ShareEl& a) -> ShareEl {return a + ShareEl(1);}
            );
    assert(circ.num_outputs() == 1);
    assert(circ.num_oWires(0) == 1);

    return liftGF<K_EXT>(circ_out);
}

/**
 * Recover the final coefficient for the polynomial h(x),
 * knowing that the sum `h(0) + ... + h(COMPRESSION - 1) = sum`
 */
CheckEl recover_final_coefficient(const std::array<CheckEl, 2*COMPRESSION - 1>& poly, const CheckEl& sum) {
    if constexpr(COMPRESSION == 2) {
        return sum - poly[1];
    } else {
        static_assert(COMPRESSION == 2, "Not implemented yet, needs solving a linear system in the general case");
    }
}

std::tuple<CheckEl, std::vector<CheckEl>, std::vector<CheckEl>> add_check_and_compress(
        CheckEl innerprod,
        const std::vector<CheckEl>& xs,
        const std::vector<CheckEl>& ys,
        FSProofStream& proof,
        GFReader<K_EXT>& preprocessing,
        GFWriter<K_EXT>& output) {

    std::array<CheckEl, 2*COMPRESSION - 1> product_poly;
    for (int i = 0; i < 2*COMPRESSION - 2; i++) { // First deg out of deg + 1 coefficients
        product_poly[i] = preprocessing.next() - proof.nextC();
    }
    // Recover the final coefficient from z
    product_poly[2 * COMPRESSION - 2] = recover_final_coefficient(product_poly, innerprod);

    PRNG gen;
    proof.hash_seed(gen);
    CheckEl r = CheckEl::random(gen);

    CheckEl z = poly_eval(product_poly, r);
    std::vector<CheckEl> newxs, newys;
    int i;
    auto preproc = interpolate_preprocess(COMPRESSION, r);
    for (i = 0; i <= xs.size() - COMPRESSION; i += COMPRESSION) {
        newxs.push_back(interpolate_with_preprocessing(preproc, std::vector<CheckEl>(xs.begin() + i, xs.begin() + i + COMPRESSION)));
        newys.push_back(interpolate_with_preprocessing(preproc, std::vector<CheckEl>(ys.begin() + i, ys.begin() + i + COMPRESSION)));
    }
    if (i < xs.size()) {
        for (auto it = xs.begin() + i; it != xs.end(); it++) {
            *it;
        }
        std::vector<CheckEl> xpts{xs.begin() + i, xs.end()};
        xpts.resize(COMPRESSION, CheckEl{0});
        newxs.push_back(interpolate_with_preprocessing(preproc, xpts));

        std::vector<CheckEl> ypts{ys.begin() + i, ys.end()};
        ypts.resize(COMPRESSION, CheckEl{0});
        newys.push_back(interpolate_with_preprocessing(preproc, ypts));
    }
    
    return {z, newxs, newys};
}

bool open_and_check(Player& me, const std::shared_ptr<BufferBitWriter>& output_writer) {
    Data mystuff = output_writer->drain();
    me.send_all(mystuff, 0);
    std::vector<Data> raw_shares = me.recv_from_all(0);
    raw_shares[me.player_idx] = std::move(mystuff);

    std::vector<GFReader<K_EXT>> all_shares;
    for (int i = 1; i <= N; i++)
        all_shares.emplace_back(std::make_shared<BufferBitReader>(std::move(raw_shares[i])));

    std::array<CheckEl, N> xcoords;
    for (int i = 0; i < N; i++) xcoords[i] = liftGF<K_EXT>(ShareEl(i + 1));
    std::array<CheckEl, N> shares;
    auto populate = [&shares, &all_shares]() { for (int j = 0; j < N; j++) shares[j] = all_shares[j].next(); };

    // Final mult check
    populate();
    auto [valA, cheatersA] = decode<T, T>(xcoords, shares);
    complain_cheaters(cheatersA, "Opening of final mult");
    populate();
    auto [valB, cheatersB] = decode<T, T>(xcoords, shares);
    complain_cheaters(cheatersB, "Opening of final mult");
    populate();
    auto [valC, cheatersC] = decode<T, T>(xcoords, shares);
    complain_cheaters(cheatersC, "Opening of final mult");
    if (valA[0] * valB[0] != valC[0]) {
        std::cerr << "Final multiplication is incorrect" << std::endl;
        return false;
    }

    // Circuit output
    populate();
    auto [val, cheaters] = decode<T, T>(xcoords, shares);
    complain_cheaters(cheaters, "Opening of the circuit output");
    if (val[0] != CheckEl{0}) {
        std::cerr << "Circuit output does not reconstruct to 0" << std::endl;
        return false;
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
                auto preprocessing_reader = std::make_shared<FileBitReader>("Player" + std::to_string(me.player_idx) + ".pre");
                GFReader<K> preprocessing(preprocessing_reader);
                GFReader<K_EXT> preprocessingC(preprocessing_reader);
                FSProofStream proof(std::move(proof_raw));

                std::vector<CheckEl> As, Bs, Cs;
                CheckEl circ_out = evaluate_circuit(circ, proof, preprocessing, As, Bs, Cs);

                // ZK masking point
                As.push_back(preprocessingC.next() - proof.nextC());
                Bs.push_back(preprocessingC.next() - proof.nextC());
                Cs.push_back(preprocessingC.next() - proof.nextC());

                // Randomization to inner product triple
                PRNG gen;

                auto output_writer = std::make_shared<BufferBitWriter>();
                GFWriter<K_EXT> output(output_writer);

                proof.hash_seed(gen);
                CheckEl innerprod = randomize_to_inner_product(As, Cs, gen);

                while (As.size() > 1) {
                    std::tie(innerprod, As, Bs) = add_check_and_compress(innerprod, As, Bs, proof, preprocessingC, output);
                }

                // To open: the final multiplication
                output.next(As[0]);
                output.next(Bs[0]);
                output.next(innerprod);

                // check circuit output
                output.next(circ_out);

                // open everything and check
                bool ok = open_and_check(me, output_writer);
                return ok;
            },

            [](bool success, double time_taken, int nruns) {
                std::cout << "Proof(s) " << (success ? "accepted" : "rejected") << ".\n";
                std::cout << "Performed " << nruns << " verifier executions in (median) " << time_taken << " seconds." << std::endl;
            });
}
