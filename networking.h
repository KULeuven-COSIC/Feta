/*
Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/
#pragma once

#include <stdexcept>
#include <memory>
#include <vector>

#include "openssl/ec.h"
#include "openssl/ssl.h"

#include "util.h"

class Networking_error : public std::runtime_error {
    public:
        Networking_error(const char* what) : std::runtime_error(what) {}
};
class SSL_error : public std::runtime_error {
    public:
        SSL_error(const char* what) : std::runtime_error(what) {}
};

using CTX_t = std::unique_ptr<SSL_CTX, void(*)(SSL_CTX*)>;
using SSL_t = std::unique_ptr<SSL, void(*)(SSL*)>;
using EC_KEY_t = std::unique_ptr<EC_KEY, void(*)(EC_KEY*)>;

class NetworkInfo {
    public:
        NetworkInfo(unsigned int me, std::istream& network_config, unsigned int N);
        ~NetworkInfo();

        void close_connection(int peer);

        void read(int peer, uint8_t* data, int length);
        void write(int peer, const uint8_t* data, int length);

        Data sign(const Data& data);
        bool verify(int peer, const Data& data, const Data& signature);

    private:
        unsigned int N;
        unsigned int m_me;
        int m_ssock;
        std::vector<int> m_csocks;
        CTX_t m_ctx;
        std::vector<SSL_t> m_ssl;
        std::vector<EC_KEY_t> m_sig_keys;
};
