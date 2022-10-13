/*
Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/
#include "player.h"

#include <cassert>

#include "util.h"

Player::Player(int idx, std::istream& network_config, int N) : player_idx(idx), N(N), m_network(idx, network_config, N) {
}

void Player::close_connection(int peer) {
    m_network.close_connection(peer);
}

template <bool sign>
Data Player::recv_from(int player) {
    uint8_t buf[4];
    m_network.read(player, &buf[0], 4);
    int length = BYTES_TO_INT(&buf[0]);
    Data res(length, 0);
    m_network.read(player, res.data(), length);
    if (sign) {
        Data sig = recv_from<false>(player);
        if (!m_network.verify(player, res, sig)) {
            throw invalid_signature(player);
        }
    }
    return res;
}
template Data Player::recv_from<false>(int);
template Data Player::recv_from<true>(int);

template <bool sign>
std::vector<Data> Player::recv_from_all(int skip) {
    std::vector<Data> res;
    for (int i = 0; i <= N; i++) {
        if (i == player_idx || i == skip) {
            res.emplace_back(0);
        } else {
            res.emplace_back(recv_from<sign>(i));
        }
    }
    return res;
}
template std::vector<Data> Player::recv_from_all<false>(int);
template std::vector<Data> Player::recv_from_all<true>(int);

template <bool sign>
void Player::send_to(int player, const Data& data) {
    uint8_t buf[4];
    INT_TO_BYTES(&buf[0], data.size());
    m_network.write(player, buf, 4);
    m_network.write(player, data.data(), data.size());
    if (sign) {
        Data sig = m_network.sign(data);
        send_to<false>(player, sig);
    }
}
template void Player::send_to<false>(int, const Data&);
template void Player::send_to<true>(int, const Data&);

void Player::send_to_with_sig(int player, const Data& data, const Data& sig) {
    send_to<false>(player, data);
    send_to<false>(player, sig);
}

template <bool sign>
void Player::send_all(const Data& data, int skip) {
    if (sign) {
        Data sig = m_network.sign(data);
        for (int i = 0; i <= N; i++) {
            if (i == player_idx || i == skip) continue;
            send_to_with_sig(i, data, sig);
        }
    } else {
        for (int i = 0; i <= N; i++) {
            if (i == player_idx || i == skip) continue;
            send_to<sign>(i, data);
        }
    }
}
template void Player::send_all<false>(const Data&, int);
template void Player::send_all<true>(const Data&, int);

void Player::commit_open_seed(PRNG& gen, int skip) {
    Data my_seed(SEED_SIZE, 0);
    gen.get_random_bytes(my_seed);
    Data commitment = Hash(my_seed);
    
    send_all(commitment, skip);
    auto all_commitments = recv_from_all(skip);

    send_all(my_seed, skip);
    auto all_seeds = recv_from_all(skip);

    for (int i = 0; i <= N; i++) {
        if (i == skip || i == player_idx) continue;
        if (Hash(all_seeds[i]) != all_commitments[i] || all_seeds[i].size() != SEED_SIZE) {
            throw std::runtime_error("Player " + std::to_string(i) + " is trying to cheat while establishing a seed.");
        }
        for (size_t j = 0; j < SEED_SIZE; j++) {
            my_seed[j] ^= all_seeds[i][j];
        }
    }

    gen.SetSeedFromRandom(my_seed.data());
}

void Player::sync() {
    send_all({1});
    recv_from_all();
}
