/*
Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/
#include "io.h"

#include <cassert>

/****** Reader ******/

bool BitReader::getbit() {
    if (!m_bits_buffered) fetch();
    m_bits_buffered--;
    bool res = m_buffer & 1;
    m_buffer >>= 1;
    return res;
}

void FileBitReader::fetch() {
    m_bits_buffered = 8;
    assert(m_file.good());
    m_file.get(m_buffer);
}

void BufferBitReader::fetch() {
    if (m_idx >= m_data.size()) throw IO_error("Out of data in buffer");
    m_bits_buffered = 8;
    m_buffer = m_data[m_idx++];
}

/****** Writer ******/

void BitWriter::putbit(bool bit) {
    if (m_bits_buffered >= 8) flush();
    m_buffer |= bit << m_bits_buffered;
    m_bits_buffered++;
}

void FileBitWriter::flush() {
    m_file.put(m_buffer);
    assert(m_file.good());
    m_buffer = 0;
    m_bits_buffered = 0;
}

void BufferBitWriter::flush() {
    m_data.push_back(m_buffer);
    m_buffer = 0;
    m_bits_buffered = 0;
}

Data BufferBitWriter::drain() {
    flush();
    Data res = std::move(m_data);
    m_data.clear();
    return res;
}

void HashableBufferBitWriter::hash_seed(PRNG& gen) {
    Data tohash;
    if (m_offset) { // If we've previously skipped stuff, chain it in
        tohash.insert(tohash.end(), std::begin(m_chain_state), std::end(m_chain_state));
    }
    tohash.insert(tohash.end(), m_data.begin() + m_offset, m_data.end());
    tohash.push_back(m_buffer);
    tohash.push_back(m_bits_buffered); // Prevent collisions
    Data H = Hash(tohash);
    gen.SetSeedFromRandom(H.data());
    if (m_data.size() > sizeof(m_chain_state)) {
        m_offset = m_data.size() - 1;
        gen.get_random_bytes(m_chain_state, sizeof(m_chain_state));
    }
}
