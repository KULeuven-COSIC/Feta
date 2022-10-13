/**
 * Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.
 * All rights reserved
 *  
 * Some of the code in this file was copied or adapted from the SCALE-MAMBA open source repository
 * See the `SCALE_MAMBA.license` file for copyright details
 */
#include "networking.h"

#include <cstring>
#include <istream>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <resolv.h>


#include "openssl/ec.h"
#include "openssl/ecdsa.h"
#include "openssl/err.h"
#include "openssl/pem.h"
#include "openssl/ssl.h"

#include "util.h"

namespace {
    EC_KEY_t wrap_EC_KEY(EC_KEY* ptr) {
        return EC_KEY_t(ptr, EC_KEY_free);
    }

    SSL_t wrap_SSL(SSL* ptr) {
        return SSL_t(ptr, SSL_free);
    }

    CTX_t wrap_CTX(SSL_CTX* ptr) {
        return CTX_t(ptr, SSL_CTX_free);
    }

    CTX_t InitCTX(void)
    {
        const SSL_METHOD *method;

        method= TLS_method();     /* create new server-method instance */
        CTX_t ctx= wrap_CTX(SSL_CTX_new(method)); /* create new context from method */

        if (ctx == NULL)
        {
            ERR_print_errors_fp(stdout);
            throw SSL_error("InitCTX");
        }

        SSL_CTX_set_mode(ctx.get(), SSL_MODE_AUTO_RETRY);

        return ctx;
    }

    void LoadCertificates(SSL_CTX *ctx, const char *CertFile, const char *KeyFile)
    {
        /* set the local certificate from CertFile */
        if (SSL_CTX_use_certificate_file(ctx, CertFile, SSL_FILETYPE_PEM) <= 0)
        {
            ERR_print_errors_fp(stdout);
            throw SSL_error("LoadCertificates 1");
        }
        /* set the private key from KeyFile (may be the same as CertFile) */
        if (SSL_CTX_use_PrivateKey_file(ctx, KeyFile, SSL_FILETYPE_PEM) <= 0)
        {
            ERR_print_errors_fp(stdout);
            throw SSL_error("LoadCertificates 2");
        }
        /* verify private key */
        if (!SSL_CTX_check_private_key(ctx))
        {
            throw SSL_error("Private key does not match the public certificate");
        }
    }

    /**
     * Important note: this also verifies the peer is who we expect it to be,
     * preventing a malicious party of connecting multiple times to us on the server socket.
     */
    void ShowCerts(SSL *ssl, const std::string CommonName)
    {
        X509 *cert;

        cert= SSL_get_peer_certificate(ssl); /* Get certificates (if available) */
        if (cert != NULL)
        {
            char buffer[256];
            X509_NAME_get_text_by_NID(X509_get_subject_name(cert), NID_commonName,
                    buffer, 256);
            std::string name(buffer);
            if (name.compare(CommonName) != 0)
            {
                throw SSL_error("Common name does not match what I was expecting");
            }

            X509_free(cert);
        }
        else {
            printf("No certificates.\n");
        }
    }


    CTX_t Init_SSL_CTX(unsigned int me, const std::string& basePath)
    {
        // Initialize the SSL library
        OPENSSL_init_ssl(
                OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
        CTX_t ctx= InitCTX();

        // Load in my certificates
        std::string str_crt= basePath + "/Player" + std::to_string(me) + ".crt";
        std::string str_key= basePath + "/Player" + std::to_string(me) + ".key";
        LoadCertificates(ctx.get(), str_crt.c_str(), str_key.c_str());

        // Turn on client auth via cert
        SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                NULL);

        // Load in root CA
        std::string str= basePath + "/Root.crt";
        SSL_CTX_set_client_CA_list(ctx.get(), SSL_load_client_CA_file(str.c_str()));
        SSL_CTX_load_verify_locations(ctx.get(), str.c_str(), NULL);
        return ctx;
    }

    std::vector<SSL_t> buildSSLConnections(unsigned int me, SSL_CTX* ctx, std::vector<int> csockets, unsigned int N) {
        // When communicating with player i, player me acts as server when i<me
        std::vector<SSL_t> res;
        for (unsigned int i= 0; i <= N; i++) // 1 prover + N verifiers
        {
            if (i != me)
            {
                res.emplace_back(wrap_SSL(SSL_new(ctx))); /* get new SSL state with context */
                if (i < me)
                { /* set connection socket to SSL state */
                    int ret= SSL_set_fd(res[i].get(), csockets[i]);
                    if (ret == 0)
                    {
                        printf("S: Player %d failed to SSL_set_fd with player %d\n", me, i);
                        throw SSL_error("SSL_set_fd");
                    }
                    /* do SSL-protocol accept */
                    ret= SSL_accept(res[i].get());
                    if (ret <= 0)
                    {
                        printf("S: Error in player %d accepting to player %d (ret = %d, err = %d)\n", me, i, ret, SSL_get_error(res[i].get(), ret));
                        ERR_print_errors_fp(stdout);
                        throw SSL_error("SSL_accept");
                    }
                }
                else
                { // Now client side stuff
                    int ret= SSL_set_fd(res[i].get(), csockets[i]);
                    if (ret == 0)
                    {
                        printf("C: Player %d failed to SSL_set_fd with player %d\n", me, i);
                        throw SSL_error("SSL_set_fd");
                    }
                    /* do SSL-protocol connect */
                    ret= SSL_connect(res[i].get());
                    if (ret <= 0)
                    {
                        printf("C: Error player %d connecting to player %d (ret = %d, err = %d)\n", me, i, ret, SSL_get_error(res[i].get(), ret));
                        ERR_print_errors_fp(stdout);
                        throw SSL_error("SSL_connect");
                    }
                }
                ShowCerts(res[i].get(), "Player" + std::to_string(i)); /* get cert and test common name */
            } else {
                res.emplace_back(wrap_SSL(nullptr));
            }
        }
        return res;
    }

    // Create the server socket and initialize the socket address structure
    // max is the max number of connections to expect
    int OpenListener(int port, int max)
    {
        int sd;
        struct sockaddr_in addr;
        bzero(&addr, sizeof(addr));

        sd= socket(AF_INET, SOCK_STREAM, 0);
        if (sd == -1)
        {
            throw Networking_error("Unable to open socket");
        }

        bzero(&addr, sizeof(addr));       /* Zero the struct before filling the fields */
        addr.sin_family= AF_INET;         /* Set the connection to TCP/IP */
        addr.sin_addr.s_addr= INADDR_ANY; /* Set our address to any interface */
        addr.sin_port= htons(port);       /* Set the server port number */

        int one= 1;
        int fl= setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof(int));
        if (fl < 0)
        {
            throw Networking_error("OpenListener: setsockopt : SO_REUSEADDR");
        }

        fl= setsockopt(sd, SOL_SOCKET, SO_REUSEPORT, (char *) &one, sizeof(int));
        if (fl < 0)
        {
            throw Networking_error("OpenListener: setsockopt : SO_REUSEPORT");
        }

        /* disable Nagle's algorithm */
        fl= setsockopt(sd, IPPROTO_TCP, TCP_NODELAY, (char *) &one, sizeof(int));
        if (fl < 0)
        {
            throw Networking_error("OpenListener: setsockopt : TCP_NODELAY");
        }

        /* bind serv information to mysocket
         *   - Just assume it will eventually wake up
         */
        fl= 1;
        while (fl != 0)
        {
            fl= ::bind(sd, (struct sockaddr *) &addr, sizeof(addr));
            if (fl != 0)
            {
                printf("Binding to socket on %d failed, trying again in a second ...\n",
                        port);
                sleep(1);
            }
        }

        /* start listening, allowing a queue of up to 1 pending connection */
        if (listen(sd, max) != 0)
        {
            std::string err= "Unable to listen for connections : Error code " +
                std::to_string(errno);
            throw Networking_error(err.c_str());
        }
        return sd;
    }

    // Connect for the client
    int OpenConnection(const std::string &hostname, int port)
    {
        int sd;

        sd= socket(AF_INET, SOCK_STREAM, 0);
        if (sd == -1)
        {
            throw Networking_error("Unable to open socket");
        }

        /* disable Nagle's algorithm */
        int one= 1;
        int fl= setsockopt(sd, IPPROTO_TCP, TCP_NODELAY, (char *) &one, sizeof(int));
        if (fl < 0)
        {
            throw Networking_error("OpenConnection: setsockopt : TCP_NODELAY");
        }

        struct sockaddr_in addr;
        bzero(&addr, sizeof(addr));
        addr.sin_family= AF_INET;
        addr.sin_port= htons(port);

        struct addrinfo hints, *ai= NULL, *rp;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family= AF_INET;
        hints.ai_flags= AI_CANONNAME;

        char my_name[512];
        memset(my_name, 0, 512 * sizeof(char));
        gethostname((char *) my_name, 512);

        int erp;
        for (int i= 0; i < 60; i++)
        {
            erp= getaddrinfo(hostname.c_str(), NULL, &hints, &ai);
            if (erp == 0)
            {
                break;
            }
            else
            {
                printf("Getaddrinfo on %s  has returned '%s' for %s trying again in a "
                        "second ...\n",
                        my_name, gai_strerror(erp), hostname.c_str());
                if (ai)
                    freeaddrinfo(ai);
                sleep(1);
            }
        }
        if (erp != 0)
        {
            throw Networking_error("set_up_socket:getaddrinfo");
        }

        for (rp= ai; rp != NULL; rp= rp->ai_next)
        {
            const struct in_addr *addr4=
                &((const struct sockaddr_in *) ai->ai_addr)->sin_addr;
            if (ai->ai_family == AF_INET)
            {
                memcpy((char *) &addr.sin_addr.s_addr, addr4, sizeof(in_addr));
                continue;
            }
        }
        freeaddrinfo(ai);

        // Loop until connection made
        do
        {
            fl= 1;
            while (fl == 1 || errno == EINPROGRESS)
            {
                fl= connect(sd, (struct sockaddr *) &addr, sizeof(struct sockaddr));
            }
        }
        while (fl == -1 && errno == ECONNREFUSED);
        if (fl < 0)
        {
            close(sd);
            std::string err= "Set_up_socket:connect: to " + hostname + " on port " + std::to_string(port);
            throw Networking_error(err.c_str());
        }

        return sd;
    }

    void send(int socket, uint8_t *msg, int len)
    {
        if (::send(socket, msg, len, 0) != len)
        {
            throw Networking_error("Send error - 1 ");
        }
    }

    void receive(int socket, uint8_t *msg, int len)
    {
        int i= 0, j;
        while (len - i > 0)
        {
            j= recv(socket, msg + i, len - i, 0);
            if (j < 0)
            {
                throw Networking_error("Receiving error - 1");
            }
            i= i + j;
        }
    }

    void Get_Connections(int &ssocket, std::vector<int> &csocket,
            const std::vector<std::pair<std::string, uint16_t>> &addresses, unsigned int me, unsigned int N)
    {
        // create server socket
        ssocket = OpenListener(addresses[me].second, N);
        for (unsigned int i= 0; i <= N; i++)
        {
            if (i != me)
            {
                if (i < me)
                {
                    struct sockaddr_in addr;
                    bzero(&addr, sizeof(addr));
                    socklen_t len= sizeof(addr);

                    int client= accept(ssocket, (struct sockaddr *) &addr, &len); /* accept connection as usual */
                    if (client == -1)
                    {
                        std::string err= "Unable to accept connections : Error code " +
                            std::to_string(errno);
                        throw Networking_error(err.c_str());
                    }

                    // Receive the player connected, the thread number and the connection
                    uint8_t buff[4];
                    receive(client, buff, 4);
                    int p = BYTES_TO_INT(buff);
                    csocket[p] = client;
                }
                else
                { // Now client side stuff
                    csocket[i]= OpenConnection(addresses[i].first, addresses[i].second);
                    // Send my number, my thread number and my connection
                    uint8_t buff[4];
                    INT_TO_BYTES(buff, me);
                    send(csocket[i], buff, 4);
                }
            }
        }
    }

    std::vector<EC_KEY_t> readSigKeys(const std::string& basePath, unsigned int me, unsigned int N) {
        std::vector<EC_KEY_t> res;
        for (unsigned int i = 0; i <= N; i++) {
            if (i == me) {
                FILE* f = fopen((basePath + "/Player" + std::to_string(i) + ".priv").c_str(), "r");
                res.emplace_back(wrap_EC_KEY(PEM_read_ECPrivateKey(f, NULL, NULL, NULL)));
                fclose(f);
            } else {
                FILE* f = fopen((basePath + "/Player" + std::to_string(i) + ".pub").c_str(), "r");
                res.emplace_back(wrap_EC_KEY(PEM_read_EC_PUBKEY(f, NULL, NULL, NULL)));
                fclose(f);
            }
        }
        return res;
    }
} // anonymous namespace

NetworkInfo::NetworkInfo(unsigned int me, std::istream& network_config, unsigned int N) : N(N), m_me(me), m_ssock(0), m_csocks(N + 1, 0), m_ctx(wrap_CTX(nullptr)), m_ssl() {
    // Somehow we *sometimes* get crashes from a SIGPIPE during SSL_accept, it works when ignoring the signal
    // Any function that would trigger SIGPIPE should return EPIPE otherwise anyway
    signal(SIGPIPE, SIG_IGN);
    std::string basePath;
    network_config >> basePath;

    std::vector<std::pair<std::string, uint16_t>> IPs(N + 1);
    for (unsigned int i = 0; i <= N; i++)
        network_config >> IPs[i].first >> IPs[i].second;

    Get_Connections(m_ssock, m_csocks, IPs, m_me, N);

    m_ctx = Init_SSL_CTX(m_me, basePath);
    m_ssl = buildSSLConnections(me, m_ctx.get(), m_csocks, N);

    m_sig_keys = readSigKeys(basePath, me, N);
}

NetworkInfo::~NetworkInfo() {
    // SSL stuff is taken care of by the unique_ptr deleters
    // But we need to trigger it manually to close the SSL before closing the sockets
    m_ssl.clear();
    m_ctx.reset();

    for (unsigned int i= 0; i <= N; i++)
    {
        if (i != m_me && m_csocks[i] != 0) close(m_csocks[i]);
    }

    close(m_ssock);
}

void NetworkInfo::close_connection(int peer) {
    if (m_csocks[peer]) close(m_csocks[peer]);
    m_ssl[peer].reset();
}

void NetworkInfo::read(int peer, uint8_t* data, int length) {
    if (!m_ssl[peer]) return; // Shouldn't happen, but as a safety measure, if we closed the connection, just give garbage
    int received = 0;
    while (received < length) {
        int round_received = SSL_read(m_ssl[peer].get(), &data[received], length - received);
        if (round_received <= 0) {
            throw Networking_error("Read failed");
        }
        received += round_received;
    }
    if (received != length) {
        throw Networking_error("Incorrect amount of data received");
    }
}

void NetworkInfo::write(int peer, const uint8_t* data, int length) {
    if (!m_ssl[peer]) return; // Shouldn't happen, but as a safety measure, if we closed the connection, don't send
    if (SSL_write(m_ssl[peer].get(), data, length) != length)
    {
      throw Networking_error("Send failed");
    }
}

Data NetworkInfo::sign(const Data& data) {
    // Could be made more efficient with ECDSA_sign_setup
    Data H = Hash(data);
    Data sig(ECDSA_size(m_sig_keys[m_me].get()));
    // "The parameter type is currently ignored."
    unsigned int siglen = 0;
    int success = ECDSA_sign(0, H.data(), H.size(), sig.data(), &siglen, m_sig_keys[m_me].get());
    if (!success) {
        ERR_print_errors_fp(stdout);
        throw SSL_error("signature failed");
    }
    sig.resize(siglen);
    return sig;
}

bool NetworkInfo::verify(int peer, const Data& data, const Data& sig) {
    Data H = Hash(data);
    int res = ECDSA_verify(0, H.data(), H.size(), sig.data(), sig.size(), m_sig_keys[peer].get());
    if (res < 0) {
        ERR_print_errors_fp(stdout);
        throw SSL_error("signature verification failed");
    }
    return res;
}
