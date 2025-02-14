//
// Created by BaseDCaTx on 2/14/2025.
//

#ifndef XHTTP_REVISED_COMPRESSOR_H
#define XHTTP_REVISED_COMPRESSOR_H

#include <vector>
#include <cstdint>
#include <zlib.h>
#include <stdexcept>

class ZCompressor {

public:
    static std::vector<uint8_t> compress(const std::vector<uint8_t> &input) {
        z_stream stream{};
        if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
            throw std::runtime_error("Failed to initialize compression");
        }

        std::vector<uint8_t> output(input.size());
        stream.next_in = const_cast<uint8_t *>(input.data());
        stream.avail_in = input.size();

        int ret;
        do {
            output.resize(output.size() * 2);
            stream.next_out = output.data() + stream.total_out;
            stream.avail_out = output.size() - stream.total_out;
            ret = deflate(&stream, Z_FINISH);
        } while (ret == Z_OK || ret == Z_BUF_ERROR);

        if (ret != Z_STREAM_END) {
            deflateEnd(&stream);
            throw std::runtime_error("Compression failed");
        }
        output.resize(stream.total_out);
        deflateEnd(&stream);
        return output;
    }

    static std::vector<uint8_t> decompress(const std::vector<uint8_t> &input) {
        z_stream stream{};
        if (inflateInit2(&stream, 15 + 16) != Z_OK) {
            throw std::runtime_error("Failed to initialize decompression");
        }

        std::vector<uint8_t> output(input.size() * 2);
        stream.next_in = const_cast<uint8_t *>(input.data());
        stream.avail_in = input.size();

        int ret;

        do {
            output.resize(output.size() * 2);
            stream.next_out = output.data() + stream.total_out;
            stream.avail_out = output.size() - stream.total_out;
            ret = inflate(&stream, Z_NO_FLUSH);
        } while (ret == Z_OK || ret == Z_BUF_ERROR);

        if (ret != Z_STREAM_END) {
            inflateEnd(&stream);
            throw std::runtime_error("Decompression failed");
        }

        output.resize(stream.total_out);
        inflateEnd(&stream);
        return output;
    }
};

#endif //XHTTP_REVISED_COMPRESSOR_H
