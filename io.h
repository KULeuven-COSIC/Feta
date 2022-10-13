/*
Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/
#pragma once

#include "arith.h"
#include "networking.h"

#include <fstream>
#include <memory>
#include <stdexcept>

class IO_error : public std::runtime_error {
    public:
        IO_error(const char* what) : std::runtime_error(what) {}
};

class BitReader {
    public:
        BitReader() : m_buffer(0), m_bits_buffered(0) {}
        virtual ~BitReader() {}

        bool getbit();
    
    protected:
        virtual void fetch() = 0;

        char m_buffer;
        char m_bits_buffered;
};

class BitWriter {
    public:
        BitWriter() : m_buffer(0), m_bits_buffered(0) {}
        virtual ~BitWriter() {} // Cannot call virtual `flush()` here, should generally happen in subclasses

        void putbit(bool bit);

    protected:
        virtual void flush() = 0;

        char m_buffer;
        char m_bits_buffered;
};

class FileBitReader : public BitReader {
    public:
        FileBitReader(std::string filename) : m_file(filename, std::ios_base::binary) { }
        FileBitReader(FileBitReader&&) = default; // copy-constructor implicitly deleted already bc of file member

    protected:
        void fetch() override;

    private:
        std::ifstream m_file;
};

class BufferBitReader : public BitReader {
    public:
        BufferBitReader(Data&& data): m_data(std::move(data)), m_idx(0) {}

    protected:
        void fetch() override;

    private:
        Data m_data;
        size_t m_idx;
};

class FileBitWriter : public BitWriter {
    public:
        FileBitWriter(std::string filename) : m_file(filename, std::ios_base::binary) { }
        FileBitWriter(FileBitWriter&&) = default; // copy-constructor implicitly deleted already bc of file member
        ~FileBitWriter() { flush(); }

    protected:
        void flush() override;

        std::ofstream m_file;
};

class BufferBitWriter : public BitWriter {
    public:
        BufferBitWriter() : m_data(0) {}
        ~BufferBitWriter() { flush(); }

        Data drain();

    protected:
        void flush() override;

        Data m_data;
};

// TODO? Implement with a proper rolling hash rather than the PRNG chaining state?
class HashableBufferBitWriter : public BufferBitWriter {
    public:
        HashableBufferBitWriter() : BufferBitWriter() {}
        ~HashableBufferBitWriter() {}

        // Useful for when we need multiple Fiat-Shamir transformations, but don't want to drain
        void hash_seed(PRNG& gen);

    private:
        std::size_t m_offset = 0;
        uint8_t m_chain_state[32] = {0};
};

template <int k>
class GFReader {
    public:
        GFReader(std::string filename) : m_reader(std::make_shared<FileBitReader>(filename)) {}
        GFReader(Data&& d) : m_reader(std::make_shared<BufferBitReader>(std::move(d))) {}
        GFReader(const std::shared_ptr<BitReader>& reader) : m_reader(reader) {}

        GF2k<k> next() {
            std::array<bool, k> bits;
            for (int i = 0; i < k; i++) {
                bits[i] = m_reader->getbit();
            }
            return GF2k<k>::from_bits(bits);
        }

    private:
        std::shared_ptr<BitReader> m_reader;
};

template <int k>
class GFWriter {
    public:
        GFWriter(std::string filename) : m_writer(std::make_shared<FileBitWriter>(filename)) { }
        GFWriter(const std::shared_ptr<BitWriter>& writer) : m_writer(writer) {}

        void next(GF2k<k> el) {
            for (bool bit : el.to_bits()) {
                m_writer->putbit(bit);
            }
        }

    private:
        std::shared_ptr<BitWriter> m_writer;
};
