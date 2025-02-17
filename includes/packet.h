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

std::string HTTP_TEMPLATE_BASIC = R"(HTTP/1.1\r\n200/r/n[crlf]/r/n)";
std::string HTTP_TEMPLATE_PACKET_BODY_REGEX = R"(/r/n(.*?)/r/n)";
std::string HTTP_PACKET_BODY_PLACEHOLDER = "[crlf]";

class Packet {

private:
    uint16_t m_flag{};

public:

    uint32_t m_msg_len{};           // Message length// Size of this structure
    std::string m_message{}; // Dynamic-size buffer for the m_message
    std::string m_response_format{HTTP_TEMPLATE_BASIC};


    // Constructor for Packet
    explicit Packet(const std::string &msg) : m_message(msg), m_msg_len(msg.size()), m_flag(Flags::COMPRESSION_FLAG) {}

    explicit Packet() = default;

    Packet *setFlag(uint16_t f = 0) {
        this->m_flag = this->m_flag | f;
        return this;
    }

    [[nodiscard]] uint16_t getFlag() const {
        return this->m_flag;
    }

    [[nodiscard]] bool checkFlag(uint16_t f) const {
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

        buffer.resize(sizeof(pck.m_msg_len) + sizeof(pck.getFlag()) + pck.m_message.size());
        size_t offset = 0;

        uint32_t message_len = htonl(pck.m_msg_len);
        std::memcpy(buffer.data() + offset, &message_len, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        uint16_t flags = htons(pck.getFlag());
        std::memcpy(buffer.data() + offset, &flags, sizeof(uint16_t));
        offset += sizeof(uint16_t);

        std::cout << "\nMessage size: " << pck.m_message.size() << " Offset: " << offset << "\n\n";

        std::memcpy(buffer.data() + offset, pck.m_message.data(), pck.m_message.size());
        offset += pck.m_message.size();

        std::cout << "\nOffset: " << offset << "\n\n";
        Base64 b64;
        std::vector<uint8_t> e_buf = AESGCM::encrypt(ZCompressor::compress(buffer));
        std::string str =  b64.base64_encode(e_buf);
        std::vector<uint8_t> ret{str.begin(), str.end()};
        return ret;
    }

    static Packet decode(const std::vector<uint8_t> &buffer) {

        if (buffer.empty()) {
            Packet pck;
            pck.setFlag(Flags::CONNECTION_CLOSED);
            return pck;
        }

        Base64 b64;
        std::string buf_str{buffer.begin(), buffer.end()};
        std::vector b64_decoded_buf = b64.base64_decode(buf_str);
        std::vector<uint8_t> decrypted = ZCompressor::decompress(AESGCM::decrypt(b64_decoded_buf));

        ssize_t offset = 0;

        uint32_t netMsgLength;
        std::memcpy(&netMsgLength, decrypted.data() + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        uint16_t flag;
        std::memcpy(&flag, decrypted.data() + offset, sizeof(uint16_t));
        offset += sizeof(uint16_t);

        std::string message;
        message.insert(message.begin(), decrypted.begin() + offset, decrypted.end());

        std::cout << "\n\n-------- DECODE LOGS START ------\n\n";
        for (auto i : decrypted) {
            std::cout << (char) i << ' ';
        }
        std::cout << "\n\n-------- DECODE LOGS END ------\n\n";

        std::cout << "Decoded message size: " << message.size() << "\n\n";
        Packet pck = Packet(message);
        pck.setFlag(ntohs(flag));
        pck.m_msg_len = ntohl(netMsgLength);

        return pck;
    }

    static ssize_t frame_to(std::vector<uint8_t> &buf, int sock, std::string &header = HTTP_TEMPLATE_BASIC) {

        std::string h_template = header;
        std::string data = {buf.begin(), buf.end()};
        ulong index = h_template.find(HTTP_PACKET_BODY_PLACEHOLDER);

        if (index == std::string::npos) {
            return -1;
        }

        std::cout << "\nHeaderSize: " << header.size() << "\n\n";

        h_template.replace(index, HTTP_PACKET_BODY_PLACEHOLDER.size(), data);
        ssize_t total_sent = 0;

        std::cout << "\n---" << h_template << "---\n";
        std::vector<uint8_t> temp{h_template.begin(), h_template.end()};
        return frame_to_proxy(temp, sock);
    }

    static std::string unframe (Packet &packet) {


        std::regex pattern(HTTP_TEMPLATE_PACKET_BODY_REGEX);
        std::smatch matcher;

        if (std::regex_search(packet.m_message, matcher, pattern)) {
            std::cout << "Found: " << matcher.size() << " matches\n" << matcher[0] << "\n";
            return matcher[1];
        }


        return {};
    }


// I was forced to put him here, would refactor soon
    class Base64 {
    private:
        const std::string base64_chars =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "abcdefghijklmnopqrstuvwxyz"
                "0123456789+/";

        static inline bool is_base64(uint8_t c) {
            return (isalnum(c) || (c == '+') || (c == '/'));
        }

    public:

        std::string base64_encode(std::vector<uint8_t> &buf) {
            std::string ret;
            int i = 0;
            int j = 0;
            uint8_t char_array_3[3];
            uint8_t char_array_4[4];
            size_t bufLen = buf.size();
            uint8_t* buf2 = buf.data();

            while (bufLen--) {
                char_array_3[i++] = *(buf2++);
                if (i == 3) {
                    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                    char_array_4[3] = char_array_3[2] & 0x3f;

                    for(i = 0; (i <4) ; i++)
                        ret += base64_chars[char_array_4[i]];
                    i = 0;
                }
            }

            if (i)
            {
                for(j = i; j < 3; j++)
                    char_array_3[j] = '\0';

                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                char_array_4[3] = char_array_3[2] & 0x3f;

                for (j = 0; (j < i + 1); j++)
                    ret += base64_chars[char_array_4[j]];

                while((i++ < 3))
                    ret += '=';
            }

            return ret;
        }



        std::vector<uint8_t> base64_decode(std::string const& encoded_string) {
            int in_len = encoded_string.size();
            int i = 0;
            int j = 0;
            int in_ = 0;
            uint8_t char_array_4[4], char_array_3[3];
            std::vector<uint8_t> ret;

            while (in_len-- && ( encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
                char_array_4[i++] = encoded_string[in_]; in_++;
                if (i ==4) {
                    for (i = 0; i <4; i++)
                        char_array_4[i] = base64_chars.find(char_array_4[i]);

                    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
                    char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
                    char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

                    for (i = 0; (i < 3); i++)
                        ret.push_back(char_array_3[i]);
                    i = 0;
                }
            }

            if (i) {
                for (j = i; j <4; j++)
                    char_array_4[j] = 0;

                for (j = 0; j <4; j++)
                    char_array_4[j] = base64_chars.find(char_array_4[j]);

                char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
                char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
                char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

                for (j = 0; (j < i - 1); j++) ret.push_back(char_array_3[j]);
            }

            return ret;
        }



    };


    static ssize_t frame_to_proxy(std::vector<uint8_t> &buf, int sock) {

        ssize_t total_sent = 0;

        //  std::cout << "\n---" << data << "---\n";


        while (total_sent < buf.size()) {
            size_t bytes_to_send = std::min((size_t) CHUNK_N_BYTES, buf.size() - total_sent);
            ssize_t byte_sent = write(sock, buf.data() + total_sent, bytes_to_send);
            total_sent += byte_sent;
        }

        return total_sent;
    }

    static ssize_t frame_from_proxy(int proxy_sock, std::map<int, std::vector<uint8_t>> &buf_map, Packet &packet) {

        uint8_t temp[CHUNK_N_BYTES];
        ssize_t bytes_read = read(proxy_sock, temp, CHUNK_N_BYTES);
        buf_map.at(proxy_sock).reserve(CHUNK_N_BYTES + buf_map.at(proxy_sock).size());

        if (bytes_read > 0) {
            buf_map.at(proxy_sock).insert(buf_map.at(proxy_sock).end(), temp, temp + bytes_read);
            packet.m_message = std::string(buf_map.at(proxy_sock).begin(), buf_map.at(proxy_sock).end());
            packet.setFlag(Flags::IS_RESPONSE_FLAG);
            buf_map.erase(proxy_sock);
        }

        if (bytes_read == 0) {
            packet.setFlag(Flags::CONNECTION_CLOSED);
            return 0;
        } else {
            if (errno == EAGAIN) {
                return -1;
            }
        }


        return bytes_read;
    }


};

#endif //XHTTP_PACKET_H
