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


enum Flags {
    COMPRESSION_FLAG = 0x0100,
    CONTINUATION_FLAG = 0x0200,
    IS_RESPONSE_FLAG = 0x0300,
    IS_CHUNK_FLAG = 0x0400,
    IS_REQUEST_FLAG = 0x0500,
    CONNECTION_CLOSED = 0x0600,
    USER_ACCOUNT_VALID = 0x0700
};

#define CHUNK_N_BYTES (1024 * 4)

std::string HTTP_TEMPLATE_BASIC = R"(HTTP/1.1 200\r\n[crlf]\r\n)";
std::string HTTP_TEMPLATE_PACKET_BODY_REGEX = R"(\\r\\n(.*)\\r\\n)";
std::string HTTP_PACKET_BODY_PLACEHOLDER = "[crlf]";

class Packet {

public:

    uint32_t m_msg_len{};           // Message length// Size of this structure
    uint16_t m_flag{};
    std::string m_message{}; // Dynamic-size buffer for the m_message
    std::string m_response_format{HTTP_TEMPLATE_BASIC};


    // Constructor for Packet
    explicit Packet(const std::string &msg) : m_message(msg), m_msg_len(msg.size()), m_flag(Flags::COMPRESSION_FLAG) {}

    explicit Packet() = default;

    Packet *setFlag(uint16_t f) {
        this->m_flag = this->m_flag | f;
        return this;
    }

    uint16_t getFlag() const {
        return this->m_flag;
    }

    bool checkFlag(uint16_t f) {
        return this->m_flag & f;
    }

    // Method to print the packet details
    void printPacketDetails() const {
        std::cout << "Packet Details:\n";
        std::cout << "Message Length: " << m_msg_len << "\n";
        std::cout << "Flag: " << static_cast<uint16_t>(m_flag) << "\n";
        std::cout << "Message: ";
        for (const auto &byte: m_message) {
            std::cout << (char) byte;
        }
        std::cout << "\n";
    }
};


class BufferHandler {

public:
    static std::vector<uint8_t> encode(const Packet &pck) {
        std::vector<uint8_t> buffer{};
        buffer.resize(sizeof(pck.m_msg_len) + sizeof(pck.m_flag) + pck.m_message.size());
        size_t offset = 0;

        std::memcpy(buffer.data() + offset, &pck.m_msg_len, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        std::memcpy(buffer.data() + offset, &pck.m_flag, sizeof(uint16_t));
        offset += sizeof(uint16_t);

        std::copy(pck.m_message.begin(), pck.m_message.end(), buffer.begin() + offset);

        return AESGCM::encrypt(ZCompressor::compress(buffer));
    }

    static Packet decode(const std::vector<uint8_t> &buffer) {

        if (buffer.empty()) {
            Packet pck;
            pck.setFlag(Flags::CONNECTION_CLOSED);
            return pck;
        }

        std::vector<uint8_t> decrypted = ZCompressor::decompress(AESGCM::decrypt(buffer));

        ssize_t offset = 0;
        uint32_t netMsgLength;

        std::memcpy(&netMsgLength, decrypted.data() + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        auto flag = static_cast<Flags>(decrypted[offset++]);

        std::string message(decrypted.begin() + offset, decrypted.end());

        Packet pck = Packet(message);
        pck.m_flag = flag;

        return pck;
    }

    static ssize_t frame_to(std::vector<uint8_t> &buf, int sock, std::string &header = HTTP_TEMPLATE_BASIC) {

        std::string h_template{};
        std::string data = {buf.begin(), buf.end()};
        h_template = header;
        ulong index = h_template.find(HTTP_PACKET_BODY_PLACEHOLDER);

        if (index == std::string::npos) {
            return -1;
        }


        h_template.replace(index, HTTP_PACKET_BODY_PLACEHOLDER.size(), data);
        ssize_t total_sent = 0;

        std::cout << "\n---" << h_template << "---\n";

        while (total_sent < h_template.size()) {
            size_t bytes_to_send = std::min((size_t) CHUNK_N_BYTES, h_template.size() - total_sent);
            ssize_t byte_sent = write(sock, h_template.data() + total_sent, bytes_to_send);
            total_sent += byte_sent;
        }


        return total_sent;
    }

    static Packet frame_from(int sock) {
        std::string received_data;
        ssize_t bytes_read;
        ssize_t total_bytes_read = 0;


        std::vector<uint8_t> buf;

        while (true) {
            uint8_t temp[CHUNK_N_BYTES * 16];

            ssize_t bytes_read = read(sock, temp, CHUNK_N_BYTES * 16);

            if (bytes_read == 0) {
                Packet pck = Packet();
                pck.setFlag(Flags::CONNECTION_CLOSED);
                return pck;
            } else if (bytes_read < 0) {

                if (errno == EAGAIN) {

                    std::regex pattern(HTTP_TEMPLATE_PACKET_BODY_REGEX);
                    std::smatch matcher;
                    std::string rec_data((char *) temp);

                    if (std::regex_search(rec_data, matcher, pattern)) {
                        std::string found{matcher[1]};
                        std::cout << "\n\n---FOUND---" << found << "\n";

                        Packet pck = Packet(BufferHandler::decode(std::vector<uint8_t>(found.begin(), found.end())));
                        pck.setFlag(Flags::COMPRESSION_FLAG);
                        pck.setFlag(Flags::IS_RESPONSE_FLAG);
                        return pck;
                    }

                }
            }
        }

    }


    static ssize_t frame_to_proxy(std::vector<uint8_t> &buf, int sock) {

        ssize_t total_sent = 0;
        std::string data{buf.begin(), buf.end()};

        std::cout << "\n---" << data << "---\n";

        while (total_sent < data.size()) {
            size_t bytes_to_send = std::min((size_t) CHUNK_N_BYTES, data.size() - total_sent);
            ssize_t byte_sent = write(sock, data.data() + total_sent, bytes_to_send);
            total_sent += byte_sent;
        }

        return total_sent;
    }

    static ssize_t frame_from_proxy(int proxy_sock, std::map<int, std::vector<uint8_t>> &buf_map, Packet &packet) {
        uint8_t temp[CHUNK_N_BYTES];
        ssize_t bytes_read = read(proxy_sock, temp, CHUNK_N_BYTES);
        std::vector<uint8_t> buf = buf_map.at(proxy_sock);

        if (bytes_read > 0) {
            buf.insert(buf.end(), temp, temp + bytes_read);
            packet.m_message = std::string(buf.begin(), buf.end());
            packet.setFlag(Flags::IS_RESPONSE_FLAG);
            // Since we don't care about framing here, we assume anything read is what it is and just erase our socks, from it's hashed buffer
            buf_map.erase(proxy_sock);
            return bytes_read;
        }

        if (bytes_read == 0) {
            packet.setFlag(Flags::CONNECTION_CLOSED);
            return 0;
        } else {
            if (errno == EAGAIN) {
                return -1;
            }
        }

    }


};

#endif //XHTTP_PACKET_H
