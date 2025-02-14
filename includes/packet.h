//
// Created by BaseDCaTx on 1/16/2025.
//

#ifndef XHTTP_PACKET_H
#define XHTTP_PACKET_H

#include <netinet/in.h>
#include <string>
#include <ctime>
#include <vector>
#include <iostream>
#include <memory>
#include "crypt.h"
#include <regex>
#include "utils.h"
#include "compressor.h"


enum class Flags : uint16_t {
    COMPRESSION_FLAG = 0x0100,
    CONTINUATION_FLAG = 0x0200,
    IS_RESPONSE_FLAG = 0x0300,
    IS_CHUNK_FLAG = 0x0400,
    IS_REQUEST_FLAG = 0x0500,
    CONNECTION_CLOSED = 0x0600
};

#define CHUNK_N_BYTES 1024

std::string HTTP_TEMPLATE_BASIC = R"(HTTP/1.1 200\r\n[crlf]\r\n)";
std::string HTTP_TEMPLATE_PACKET_BODY_REGEX = R"(\\r\\n(.*)\\r\\n)";
std::string HTTP_PACKET_BODY_PLACEHOLDER = "[crlf]";

class Packet {

public:

    uint32_t msgLength{};           // Message length// Size of this structure
    Flags flag;                   // Flags (using enum class)
    std::string message; // Dynamic-size buffer for the message

    // Constructor for Packet
    explicit Packet(const std::string &msg) : message(msg), msgLength(msg.size()), flag(Flags::COMPRESSION_FLAG) {}

    explicit Packet() = default;

    void setFlag(uint16_t flags) {
        this->flag = static_cast<Flags>(flags);
    }

    // Method to print the packet details
    void printPacketDetails() const {
        std::cout << "Packet Details:\n";
        std::cout << "Message Length: " << msgLength << "\n";
        std::cout << "Flag: " << static_cast<uint16_t>(flag) << "\n";
        std::cout << "Message: ";
        for (const auto &byte: message) {
            std::cout << (char) byte;
        }
        std::cout << "\n";
    }
};


class BufferHandler {

public:
    static std::vector<uint8_t> encode(const Packet &pck) {
        std::vector<uint8_t> buffer{};
        buffer.resize(sizeof(pck.msgLength) + sizeof(pck.flag) + pck.message.size());
        size_t offset = 0;

        std::memcpy(buffer.data() + offset, &pck.msgLength, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        std::memcpy(buffer.data() + offset, &pck.flag, sizeof(uint16_t));
        offset += sizeof(uint16_t);

        std::copy(pck.message.begin(), pck.message.end(), buffer.begin() + offset);

        return AESGCM::encrypt(ZCompressor::compress(buffer));
    }

    static Packet decode(const std::vector<uint8_t> &buffer) {

        if (buffer.empty()) {
            return Packet({});
        }

        std::vector<uint8_t> decrypted = ZCompressor::decompress(AESGCM::decrypt(buffer));

        ssize_t offset = 0;
        uint32_t netMsgLength;

        std::memcpy(&netMsgLength, decrypted.data() + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        auto flag = static_cast<Flags>(decrypted[offset++]);

        std::string message(decrypted.begin() + offset, decrypted.end());

        Packet pck = Packet(message);
        pck.flag = flag;

        return pck;
    }

    static ssize_t frame_to(std::vector<uint8_t> &buf, int sock, std::string &header) {
        std::string h_template{header.begin(), header.end()};

        ulong index = h_template.find(HTTP_PACKET_BODY_PLACEHOLDER);

        if (index == std::string::npos) {
            return -1;
        }

        h_template.replace(index, HTTP_PACKET_BODY_PLACEHOLDER.size(), (const char *) buf.data());
        ssize_t total_sent = 0;

        while (total_sent < h_template.size()) {
            size_t bytes_to_send = std::min((size_t) CHUNK_N_BYTES, h_template.size() - total_sent);
            ssize_t byte_sent = write(sock, h_template.data() + total_sent, bytes_to_send);
            total_sent += byte_sent;
        }


        return total_sent;
    }

    static std::vector<uint8_t> frame_from(int sock) {
        std::string received_data;
        ssize_t bytes_read;
        ssize_t total_bytes_read = 0;

        while (true) {

            std::vector<uint8_t> buffer(CHUNK_N_BYTES);
            bytes_read = read(sock, buffer.data(), CHUNK_N_BYTES);

            if (bytes_read > 0) {
                // Append the newly read da ta to our received_data vector
                total_bytes_read += bytes_read;
                received_data.insert(received_data.end(), buffer.begin(), buffer.end());

            } else if (bytes_read == 0) {
                std::cout << "Server closed connection." << std::endl;
                return {};
            } else {
                // bytes_read == -1, error occurred
                if (errno == EINTR) {
                    // Interrupted by signal, try again
                    continue;
                } else if (errno == EAGAIN) {
                    std::string rec_data(received_data.begin(), received_data.end()); // Construct string from received bytes
                    std::regex pattern(HTTP_TEMPLATE_PACKET_BODY_REGEX);
                    std::smatch matcher;

                    if (std::regex_search(rec_data, matcher, pattern)) {
                        std::string found {matcher[1]};
                        std::cout << found << "\n";
                        return std::vector<uint8_t>{found.begin(), found.end()};
                    } else {
                       // std::cerr << "Invalid data read from socket. Invalid request format, can't parse\n";
                    }
                } else {
                    perror("received failed");
                    std::cerr << "Error receiving data: " << strerror(errno) << std::endl;
                    return {}; // Or throw an exception, depending on error handling strategy
                }
            }
        }
    }
};

#endif //XHTTP_PACKET_H
