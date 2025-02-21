//
// Created by BaseDCaTx on 1/16/2025.
//

#ifndef XHTTP_PACKET_H
#define XHTTP_PACKET_H

#include <netinet/in.h>
#include <string>
#include <ctime>
#include <utility>
#include <vector>
#include <iostream>
#include <memory>
#include "crypt.h"
#include <regex>
#include <fcntl.h>
#include <zlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <utility>
#include <vector>
#include <stdexcept>
#include <netdb.h>
#include <netinet/tcp.h>
#include <openssl/types.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <cstring>
#include <iostream>
#include "logger.h"
#include <vector>
#include <array>
#include "compressor.h"
#include "utilx.h"
#include "logger.h"


enum Flags {
    COMPRESSION_FLAG = 0x0001,
    IS_RESPONSE_FLAG = 0x0002,
    IS_REQUEST_FLAG = 0x0004,
    CONNECTION_CLOSED = 0x0008,
    USER_ACCOUNT_VALID = 0x0010,
    CORRUPT_DATA = 0x0020
};

#define CHUNK_N_BYTES (1024 * 25)

std::string HTTP_TEMPLATE_BASIC = R"(HTTP/1.1\r\n200/r/n[crlf]/r/n)";
std::string HTTP_TEMPLATE_PACKET_BODY_REGEX = R"(/r/n(.*?)/r/n)";
std::string HTTP_PACKET_BODY_PLACEHOLDER = "[crlf]";
std::string HTTP_DELIM = R"(/r/n)";
#define MAX_CONNECTED_SOCKS 10000


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
        uint8_t *buf2 = buf.data();

        while (bufLen--) {
            char_array_3[i++] = *(buf2++);
            if (i == 3) {
                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                char_array_4[3] = char_array_3[2] & 0x3f;

                for (i = 0; (i < 4); i++)
                    ret += base64_chars[char_array_4[i]];
                i = 0;
            }
        }

        if (i) {
            for (j = i; j < 3; j++)
                char_array_3[j] = '\0';

            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (j = 0; (j < i + 1); j++)
                ret += base64_chars[char_array_4[j]];

            while ((i++ < 3))
                ret += '=';
        }

        return ret;
    }


    std::vector<uint8_t> base64_decode(std::string const &encoded_string) {
        int in_len = encoded_string.size();
        int i = 0;
        int j = 0;
        int in_ = 0;
        uint8_t char_array_4[4], char_array_3[3];
        std::vector<uint8_t> ret;

        while (in_len-- && (encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
            char_array_4[i++] = encoded_string[in_];
            in_++;
            if (i == 4) {
                for (i = 0; i < 4; i++)
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
            for (j = i; j < 4; j++)
                char_array_4[j] = 0;

            for (j = 0; j < 4; j++)
                char_array_4[j] = base64_chars.find(char_array_4[j]);

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (j = 0; (j < i - 1); j++) ret.push_back(char_array_3[j]);
        }

        return ret;
    }


};


class Utils {
public:
    static void str_to_uint8_vec(const std::string &str, std::vector<uint8_t> &container) {
        for (const auto &ch: str) {
            container.push_back(static_cast<uint8_t>(ch));
        }
    }

    static void set_nonblocking_socket(int sock) {
        int flags = fcntl(sock, F_GETFL, 0);
        if (flags == -1) {
            LogSystemError("fcntl failed");
        }
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    }

    static void set_tcp_no_delay(int sock) {
        socklen_t enable = 1;
        setsockopt(sock, SOL_SOCKET, TCP_NODELAY, &enable, sizeof(enable));
    }

    static void set_keepalive(int sock) {
        socklen_t enable = 1;
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable));
    }

    static void set_port_reusable(int sock) {
        socklen_t enable = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
    }

    static std::pair<std::string, size_t> extract_http_frame_pay(std::vector<uint8_t> &buf) {
        std::string buf_str(buf.begin(), buf.end());
        std::string delim = HTTP_DELIM;

        size_t start_pos = buf_str.find(delim);

        if (start_pos == std::string::npos) {
            return {"", 0};
        }

        start_pos += delim.length();

        size_t end_pos = buf_str.find(delim, start_pos);
        if (end_pos == std::string::npos) {
            return {"", 0}; // Needs more bytes!
        };

        size_t frame_len = end_pos + delim.size();
        std::string payload = buf_str.substr(start_pos, end_pos - start_pos);


        buf.erase(buf.begin(), buf.begin() + frame_len);
        return {payload, frame_len};
    }

};


class Packet {

private:
    uint16_t m_flag{};
    // Dynamic-size buffer for the m_message
    std::string m_response_format{HTTP_TEMPLATE_BASIC};
    std::string m_user_acc{};
    std::string m_user_pass{};
    std::string m_host{};
    std::string m_pid{};
    std::string device_uid{};
    std::string m_message{};

public:

    // Constructor for Packet
    explicit Packet(std::string msg) : m_message(std::move(msg)) {

    }

    explicit Packet() = default;

    [[nodiscard]] const std::string &get_host () const {
        return this->m_host;
    }

    void set_host (std::string host) {
        this->m_host = std::move(host);
    }

    [[nodiscard]] const std::string &get_message() const {
        return this->m_message;
    }


    void set_message(const uint8_t *buf, ssize_t nBytes) {
        this->m_message.clear();
        this->m_message.insert(this->m_message.end(), buf, buf + nBytes);
    }

    void set_message(std::string mMessage) {
        this->m_message.clear();
        this->m_message = std::move(mMessage);
    }

    [[nodiscard]] const std::string &get_http_response_format() const {
        return this->m_response_format;
    }

    void set_http_response_format(std::string mResponseFormat) {
        this->m_response_format.clear();
        this->m_response_format = std::move(mResponseFormat);
    }

    [[nodiscard]] const std::string &get_user_acc_name() const {
        return this->m_user_acc;
    }

    void set_user_acc_name(std::string mUserAcc) {
        this->m_user_acc.clear();
        this->m_user_acc.insert(this->m_user_acc.end(), mUserAcc.begin(), mUserAcc.end());
    }

    [[nodiscard]] const std::string &get_user_acc_pass() const {
        return this->m_user_pass;
    }

    void set_user_acc_pass(std::string mUserPass) {
        this->m_user_pass.clear();
        this->m_user_pass.insert(this->m_user_pass.end(), mUserPass.begin(), mUserPass.end());
    }

    [[nodiscard]] const std::string &get_packet_id() const {
        return this->m_pid;
    }


    void set_packet_id(std::string id) {
        this->m_pid.clear();
        this->m_pid.insert(this->m_pid.end(), id.begin(), id.end());
    }

    [[nodiscard]] const std::string &get_device_id() const {
        return this->device_uid;
    }

    void set_device_id(std::string id) {
        this->device_uid.clear();
        this->device_uid.insert(this->device_uid.end(), id.begin(), id.end());
    }

    void set_packet_flags(uint16_t f = 0) {
        this->m_flag = this->m_flag | f;
    }

    [[nodiscard]] uint16_t get_packet_flags() const {
        return this->m_flag;
    }

    [[nodiscard]] bool check_packet_flag(uint16_t f) const {
        return this->m_flag & f;
    }

    void generate_packet_id() {
        Base64 b64;
        uint32_t random_num = time(nullptr);

        std::vector<uint8_t> temp;
        temp.resize(sizeof(uint32_t));
        std::memcpy(temp.data(), &random_num, sizeof(uint32_t));

        this->m_pid = b64.base64_encode(temp);
    }

    // Method to print the packet details
    void print_packet_details() const {
        std::cout << "----- Packet Details -----\n\n";

        std::cout << "Packet ID: " << this->get_packet_id() << std::endl;
        std::cout << "  Packet ID Length: " << this->get_packet_id().size() << " bytes\n" << std::endl;

        std::cout << "Account Name: " << this->get_user_acc_name() << std::endl;
        std::cout << "  Account Name Length: " << this->get_user_acc_name().size() << " bytes\n" << std::endl;

        std::cout << "Account Password: " << this->get_user_acc_pass() << std::endl;
        std::cout << "  Account Password Length: " << this->get_user_acc_pass().size() << " bytes\n" << std::endl;

        std::cout << "Response Format (HTTP Header): " << this->get_http_response_format() << std::endl;
        std::cout << "  Response Format Length: " << this->get_http_response_format().size() << " bytes\n" << std::endl;

        std::cout << "Message: " << this->get_message() << std::endl;
        std::cout << "  Message Length: " << this->get_message().size() << " bytes\n" << std::endl;

        std::cout << "Host: " << this->get_host() << std::endl;
        std::cout << "  Host Length: " << this->get_host().size() << " bytes\n" << std::endl;

        std::cout << "\n--- Packet Flags ---\n" << std::endl;

        uint16_t flags_value = this->get_packet_flags();

        std::cout << "Raw Flags Value (uint16_t): " << flags_value << std::endl << std::endl;

        if (this->check_packet_flag(Flags::COMPRESSION_FLAG)) {
            std::cout << "  <-> COMPRESSION_FLAG is SET (Value: " << static_cast<int>(Flags::COMPRESSION_FLAG) << ")" << std::endl;
        } else {
            std::cout << "  <-> COMPRESSION_FLAG is NOT SET (Value: " << static_cast<int>(Flags::COMPRESSION_FLAG) << ")" << std::endl;
        }

        if (this->check_packet_flag(Flags::CONNECTION_CLOSED)) {
            std::cout << "  <-> CONNECTION_CLOSED is SET (Value: " << static_cast<int>(Flags::CONNECTION_CLOSED) << ")" << std::endl;
        } else {
            std::cout << "  <-> CONNECTION_CLOSED is NOT SET (Value: " << static_cast<int>(Flags::CONNECTION_CLOSED) << ")" << std::endl;
        }

        if (this->check_packet_flag(Flags::IS_REQUEST_FLAG)) {
            std::cout << "  <-> IS_REQUEST_FLAG is SET (Value: " << static_cast<int>(Flags::IS_REQUEST_FLAG) << ")" << std::endl;
        } else {
            std::cout << "  <-> IS_REQUEST_FLAG is NOT SET (Value: " << static_cast<int>(Flags::IS_REQUEST_FLAG) << ")" << std::endl;
        }

        if (this->check_packet_flag(Flags::IS_RESPONSE_FLAG)) {
            std::cout << "  <-> IS_RESPONSE_FLAG is SET (Value: " << static_cast<int>(Flags::IS_RESPONSE_FLAG) << ")" << std::endl;
        } else {
            std::cout << "  <-> IS_RESPONSE_FLAG is NOT SET (Value: " << static_cast<int>(Flags::IS_RESPONSE_FLAG) << ")" << std::endl;
        }

        if (this->check_packet_flag(Flags::USER_ACCOUNT_VALID)) {
            std::cout << "  <-> USER_ACCOUNT_VALID is SET (Value: " << static_cast<int>(Flags::USER_ACCOUNT_VALID) << ")" << std::endl;
        } else {
            std::cout << "  <-> USER_ACCOUNT_VALID is NOT SET (Value: " << static_cast<int>(Flags::USER_ACCOUNT_VALID) << ")" << std::endl << std::endl;
        }


        std::cout << "----- End Packet Details -----\n" << std::endl;
    }
};


class BufferHandler {

public:
    static std::vector<uint8_t> encode(const Packet &pck) {
        size_t offset = 0;

        size_t buffer_size = sizeof(uint16_t) // flags
                             + sizeof(uint16_t) +
                             pck.get_http_response_format().size() // response_format_size + response_format
                             + sizeof(uint16_t) +
                             pck.get_user_acc_name().size()    // user_act_name_size + user_act_name
                             + sizeof(uint16_t) + pck.get_user_acc_pass().size()    // user_pass_size + user_pass
                             + sizeof(uint16_t) + pck.get_packet_id().size()        // user_pid_size + user_pid
                             + sizeof(uint16_t) + pck.get_device_id().size()        // user_hwid_size + user_hwid
                             + sizeof(uint32_t) + pck.get_message().size() +
                             sizeof(uint16_t) + pck.get_host().size();         // user_message_size + user_message

        std::vector<uint8_t> buffer(buffer_size);

        {   // FLAG
            uint16_t flag = htons(pck.get_packet_flags());
            std::memcpy(buffer.data() + offset, &flag, sizeof(uint16_t));
            offset += sizeof(uint16_t);
        }

        {   // RESPONSE HEADER PLACEHOLDER
            uint16_t response_format_size = htons(pck.get_http_response_format().size());
            std::memcpy(buffer.data() + offset, &response_format_size, sizeof(uint16_t));
            offset += sizeof(uint16_t);
            //
            std::memcpy(buffer.data() + offset, pck.get_http_response_format().data(),
                        pck.get_http_response_format().size());
            offset += pck.get_http_response_format().size();
        }

        {   // ACCT USER NAME/NAME
            const std::string &user_act = pck.get_user_acc_name();
            uint16_t user_act_name_size = htons(user_act.size());
            std::memcpy(buffer.data() + offset, &user_act_name_size, sizeof(uint16_t));
            offset += sizeof(uint16_t);
            //
            std::memcpy(buffer.data() + offset, pck.get_user_acc_name().data(), pck.get_user_acc_name().size());
            offset += pck.get_user_acc_name().size();
        }

        {   // ACCT PASS
            const std::string &user_pass = pck.get_user_acc_pass();
            uint16_t user_pass_size = htons(user_pass.size());
            std::memcpy(buffer.data() + offset, &user_pass_size, sizeof(uint16_t));
            offset += sizeof(uint16_t);
            //
            std::memcpy(buffer.data() + offset, pck.get_user_acc_pass().data(), pck.get_user_acc_pass().size());
            offset += pck.get_user_acc_pass().size();
        }

        {   // Freenet host
            const std::string &user_host = pck.get_host();
            uint16_t user_host_size = htons(user_host.size());
            std::memcpy(buffer.data() + offset, &user_host_size, sizeof(uint16_t));
            offset += sizeof(uint16_t);
            //
            std::memcpy(buffer.data() + offset, pck.get_host().data(), pck.get_host().size());
            offset += pck.get_host().size();
        }

        {   // PACKET ID
            const std::string &user_pid = pck.get_packet_id();
            uint16_t user_pid_size = htons(user_pid.size());
            std::memcpy(buffer.data() + offset, &user_pid_size, sizeof(uint16_t));
            offset += sizeof(uint16_t);
            //
            std::memcpy(buffer.data() + offset, pck.get_packet_id().data(), pck.get_packet_id().size());
            offset += pck.get_packet_id().size();
        }

        {   // USER HWID
            const std::string &user_hwid = pck.get_device_id();
            uint16_t user_hwid_size = htons(user_hwid.size());
            std::memcpy(buffer.data() + offset, &user_hwid_size, sizeof(uint16_t));
            offset += sizeof(uint16_t);
            //
            std::memcpy(buffer.data() + offset, pck.get_device_id().data(), pck.get_device_id().size());
            offset += pck.get_device_id().size();
        }

        {   // MESSAGE
            const std::string &user_message = pck.get_message();
            uint32_t user_message_size = user_message.size();
            uint32_t user_message_size_network_order = htonl(
                    user_message_size); // Corrected: use network byte order for size
            std::memcpy(buffer.data() + offset, &user_message_size_network_order, sizeof(uint32_t));
            offset += sizeof(uint32_t);
            //
            std::memcpy(buffer.data() + offset, pck.get_message().data(), pck.get_message().size());
            offset += user_message.size();
        }

        std::cout << "-----PACKET BYTES----\n";

        for (auto &c : buffer) {
            std::cout << (char) c << " ";
        }

        std::cout << "\n---END OF PACKET BYTES---\n";


        Base64 b64;
        std::vector<uint8_t> e_buf = AESGCM::encrypt(buffer);
        std::string str = b64.base64_encode(e_buf);
        std::vector<uint8_t> ret{str.begin(), str.end()};

        return ret;
    }

    static Packet decode(std::vector<uint8_t> &buff) {
        Packet packet{};

        if (buff.empty()) {
            packet.set_packet_flags(Flags::CONNECTION_CLOSED);
            std::cout << "Decode: Empty buffer, setting CONNECTION_CLOSED flag." << std::endl;
            throw std::runtime_error("Buffer is empty!");
        }

        ssize_t offset = 0;
        uint8_t *buf;

        Base64 b64;
        std::string t_str{buff.begin(), buff.end()};
        std::vector<uint8_t> e_buf = b64.base64_decode(t_str);
        std::vector<uint8_t> decoded_buf = AESGCM::decrypt(e_buf);
        buf = decoded_buf.data();

        std::cout << "--- Decoding Packet ---" << std::endl;

        std::cout << "-----PACKET BYTES----\n";

        for (auto &c : decoded_buf) {
            std::cout << (char) c << " ";
        }

        std::cout << "\n---END OF PACKET BYTES---\n";


        {   // FLAG
            uint16_t flags_network_order;
            std::memcpy(&flags_network_order, buf + offset, sizeof(uint16_t));
            packet.set_packet_flags(ntohs(flags_network_order));
            std::cout << "Flags (Network Byte Order): " << flags_network_order << std::endl;
            std::cout << "Flags (Host Byte Order): " << ntohs(flags_network_order) << ", Flags enum value: "
                      << static_cast<int>(packet.get_packet_flags()) << std::endl;
            offset += sizeof(uint16_t);
        }

        {   // RESPONSE HEADER PLACEHOLDER
            uint16_t response_header_size_network_order;
            std::memcpy(&response_header_size_network_order, buf + offset, sizeof(uint16_t));
            std::cout << "Response Header Size (Network): " << response_header_size_network_order << std::endl;
            offset += sizeof(uint16_t);
            uint16_t response_header_size_host_order = ntohs(response_header_size_network_order);
            std::cout << "Response Header Size (Host): " << response_header_size_host_order << std::endl;
            std::vector<uint8_t> temp_buffer;
            temp_buffer.resize(response_header_size_host_order);
            std::memcpy(temp_buffer.data(), buf + offset, response_header_size_host_order);
            packet.set_http_response_format(std::string{(char *) temp_buffer.data(), response_header_size_host_order});
            std::cout << "Response Format: \"" << packet.get_http_response_format() << "\"" << std::endl;
            offset += response_header_size_host_order;
        }

        {   // ACCT USER NAME/NAME
            uint16_t name_size_network_order;
            std::memcpy(&name_size_network_order, buf + offset, sizeof(uint16_t));
            std::cout << "Name Size (Network): " << name_size_network_order << std::endl;
            offset += sizeof(uint16_t);
            uint16_t name_size_host_order = ntohs(name_size_network_order);
            std::cout << "Name Size (Host): " << name_size_host_order << std::endl;
            std::vector<uint8_t> temp_buffer;
            temp_buffer.resize(name_size_host_order);
            std::memcpy(temp_buffer.data(), buf + offset, name_size_host_order);
            packet.set_user_acc_name(std::string{(char *) temp_buffer.data(), name_size_host_order});
            std::cout << "Account Name: \"" << packet.get_user_acc_name() << "\"" << std::endl;
            offset += name_size_host_order;
        }

        {   // ACCT PASS
            uint16_t pass_len_network_order;
            std::memcpy(&pass_len_network_order, buf + offset, sizeof(uint16_t));
            std::cout << "Password Length (Network): " << pass_len_network_order << std::endl;
            offset += sizeof(uint16_t);
            uint16_t pass_len_host_order = ntohs(pass_len_network_order);
            std::cout << "Password Length (Host): " << pass_len_host_order << std::endl;
            std::vector<uint8_t> temp_buffer;
            temp_buffer.resize(pass_len_host_order);
            std::memcpy(temp_buffer.data(), buf + offset, pass_len_host_order);
            packet.set_user_acc_pass(std::string{(char *) temp_buffer.data(), pass_len_host_order});
            std::cout << "Account Password: \"" << packet.get_user_acc_pass() << "\"" << std::endl;
            offset += pass_len_host_order;
        }


        {   // HOST
            uint16_t host_len_network_order{};
            std::memcpy(&host_len_network_order, buf + offset, sizeof(uint16_t));
            std::cout << "Host Length (Network): " << host_len_network_order << std::endl;
            offset += sizeof(uint16_t);
            uint16_t host_len = ntohs(host_len_network_order);
            std::cout << "Host Length (Host-BO): " << host_len << std::endl;
            std::vector<uint8_t> temp_buffer;
            temp_buffer.resize(host_len);
            std::memcpy(temp_buffer.data(), buf + offset,  host_len);
            packet.set_host(std::string{(char *) temp_buffer.data(), host_len});
            std::cout << "SNI/FREENET HOST: \"" << packet.get_user_acc_pass() << "\"" << std::endl;
            offset += host_len;
        }

        {   // PACKET ID
            uint16_t pck_id_len_network_order;
            std::memcpy(&pck_id_len_network_order, buf + offset, sizeof(uint16_t));
            std::cout << "Packet ID Length (Network): " << pck_id_len_network_order << std::endl;
            offset += sizeof(uint16_t);
            uint16_t pck_id_len_host_order = ntohs(pck_id_len_network_order);
            std::cout << "Packet ID Length (Host): " << pck_id_len_host_order << std::endl;
            std::vector<uint8_t> temp_buffer;
            temp_buffer.resize(pck_id_len_host_order);
            std::memcpy(temp_buffer.data(), buf + offset, pck_id_len_host_order);
            packet.set_packet_id(std::string{(char *) temp_buffer.data(), pck_id_len_host_order});
            std::cout << "Packet ID: \"" << packet.get_packet_id() << "\"" << std::endl;
            offset += pck_id_len_host_order;
        }

        {   // USER HWID
            uint16_t hwid_len_network_order;
            std::memcpy(&hwid_len_network_order, buf + offset, sizeof(uint16_t));
            std::cout << "HWID Length (Network): " << hwid_len_network_order << std::endl;
            offset += sizeof(uint16_t);
            uint16_t hwid_len_host_order = ntohs(hwid_len_network_order);
            std::cout << "HWID Length (Host): " << hwid_len_host_order << std::endl;
            std::vector<uint8_t> temp_buffer;
            temp_buffer.resize(hwid_len_host_order);
            std::memcpy(temp_buffer.data(), buf + offset, hwid_len_host_order);
            packet.set_device_id(std::string{(char *) temp_buffer.data(), hwid_len_host_order});
            std::cout << "HWID: \"" << packet.get_device_id() << "\"" << std::endl;
            offset += hwid_len_host_order;
        }

        {   // MESSAGE - Corrected block
            uint32_t msg_len_network_order;
            std::memcpy(&msg_len_network_order, buf + offset, sizeof(uint32_t)); // Read 32-bit size
            std::cout << "Message Length (Network): " << msg_len_network_order << std::endl;
            offset += sizeof(uint32_t);
            uint32_t msg_len_host_order = ntohl(msg_len_network_order); // Convert 32-bit size to host byte order
            std::cout << "Message Length (Host): " << msg_len_host_order << std::endl;

            if (msg_len_host_order > buff.size() - offset) { // Add a size check! VERY IMPORTANT
                std::cerr << "Error: Message length prefix indicates message data exceeds remaining buffer size!"
                          << std::endl;
                packet.set_packet_flags(Flags::CONNECTION_CLOSED);
                return packet;
            }


            std::vector<uint8_t> temp_buffer; // Use std::vector
            temp_buffer.resize(msg_len_host_order); // Resize with host byte order size (ntohl result)
            std::memcpy(temp_buffer.data(), buf + offset, msg_len_host_order); // Use host byte order size
            packet.set_message(
                    std::string{(char *) temp_buffer.data(), msg_len_host_order}); // Construct string with size
            std::cout << "Message: \"" << packet.get_message() << "\"" << std::endl;
            offset += msg_len_host_order; // Increment offset by host byte order size
        }

        std::cout << "--- Decoding Finished ---" << std::endl;
        return packet;
    }

    static std::vector<uint8_t> frame(std::vector<uint8_t> &buf, std::string &header = HTTP_TEMPLATE_BASIC) {

        std::string h_template = header;
        std::string data = {buf.begin(), buf.end()};
        ulong index = h_template.find(HTTP_PACKET_BODY_PLACEHOLDER);

        if (index == std::string::npos) {
            return {};
        }

        h_template.replace(index, HTTP_PACKET_BODY_PLACEHOLDER.size(), data);
        ssize_t total_sent = 0;

        std::cout << "\n---" << h_template << "---\n";
        return std::vector<uint8_t>{h_template.begin(), h_template.end()};
    }

   inline static ssize_t frame_to_proxy(std::vector<uint8_t> &buf, int sock) {

        ssize_t total_sent = 0;

        //  std::cout << "\n---" << data << "-- n";


        while (total_sent < buf.size()) {
            size_t bytes_to_send = std::min((size_t) CHUNK_N_BYTES, buf.size() - total_sent);
            ssize_t byte_sent = write(sock, buf.data() + total_sent, bytes_to_send);
            total_sent += byte_sent;
        }

        return total_sent;
    }


};


// don't worry about this i'd use it for DSA references

class KMPMatcher {
public:
    explicit KMPMatcher(std::string pattern) : pattern(std::move(pattern)) {
        computeLPSArray();
    }

    std::vector<int> search(const std::string &text) {
        std::vector<int> result;
        size_t textLen = text.length();
        size_t patLen = pattern.length();

        if (patLen > textLen) {
            std::cerr << "Pattern length is greater than text length\n";
            return result;
        }

        int i = 0, j = 0;
        while (i < textLen) {
            if (text[i] == pattern[j]) {
                i++;
                j++;
            }

            if (j == patLen) {
                result.push_back(i - j);
                j = lps[j - 1];
            } else if (i < textLen && text[i] != pattern[j]) {
                if (j != 0) {
                    j = lps[j - 1];
                } else {
                    i++;
                }
            }
        }
        return result;
    }

private:
    std::string pattern;
    std::vector<int> lps;

    void computeLPSArray() {
        int patLen = pattern.length();
        lps.resize(patLen);
        int len = 0;
        lps[0] = 0;
        int i = 1;

        while (i < patLen) {
            if (pattern[i] == pattern[len]) {
                len++;
                lps[i] = len;
                i++;
            } else {
                if (len != 0) {
                    len = lps[len - 1];
                } else {
                    lps[i] = 0;
                    i++;
                }
            }
        }
    }
};

class Socket {
public:
    const std::string host, service;
    int sock = -1;
    struct sockaddr *address{};

    explicit Socket(const char *serv) : service(serv) {}

    explicit Socket(const char *host, const char *serv) : host(host), service(serv) {}

    virtual void printSocketAddress() {

        if (address == nullptr) return;

        void *numericAddress;
        char addrBuff[INET6_ADDRSTRLEN];
   //     std::array<char, INET6_ADDRSTRLEN> addrBuff{};
        in_port_t port;

        switch (address->sa_family) {
            case AF_INET:
                numericAddress = &((struct sockaddr_in *) address)->sin_addr;
                port = ntohs(((struct sockaddr_in *) address)->sin_port);
                break;
            case AF_INET6:
                numericAddress = &((struct sockaddr_in6 *) address)->sin6_addr;
                port = ntohs(((struct sockaddr_in6 *) address)->sin6_port);
                break;
            default:
                std::cout << "[unknown type]\n"; // Unhandled type
                return;
        }

        // Convert the address to a readable format
        if (inet_ntop(address->sa_family, numericAddress, addrBuff, INET6_ADDRSTRLEN) == nullptr) {
            std::cout << "[Invalid address]"; // Unable to convert!
        } else {
            std::cout << addrBuff; // Print the address
            std::cout << "\nPort: " << port << std::endl;
        }
    }


};

class ClientSocket : Socket {

private:
    struct addrinfo *server_address_ll{};

public:
    ClientSocket(const char *host, const char *service) : Socket(host, service) {}

    int createSocket() {

        struct addrinfo addrCriteria{};
        addrCriteria.ai_family = AF_UNSPEC;       // IPv4 or IPv6
        addrCriteria.ai_socktype = SOCK_STREAM;  // Stream socket
        addrCriteria.ai_protocol = IPPROTO_TCP;  // TCP protocol

        int rtnVal = getaddrinfo(host.c_str(), service.c_str(), &addrCriteria, &server_address_ll);

        if (rtnVal != 0) {  // Check for errors
            LogSystemError(gai_strerror(rtnVal)); // Log human-readable error
            return -1;
        }

        // Initialize socket descriptor
        for (struct addrinfo *addr = server_address_ll; addr != nullptr; addr = addr->ai_next) {

            this->sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);

            if (this->sock < 0) {
                perror("socket() failed");
                continue;  // Try next address
            }

            size_t tBuf = 1;

            if (setsockopt(this->sock, SOL_SOCKET, SO_REUSEPORT, &tBuf, sizeof(tBuf)) < 0) {
                perror("set-sock-opt(SO_REUSE-PORT) failed");
                close(this->sock);
                this->sock = -1;
                continue;
            }

//            if (setsockopt(this->sock, IPPROTO_TCP, TCP_NODELAY, &tBuf, sizeof(tBuf)) < 0) {
//                perror("set-sock-opt(TCP_NO-DELAY) failed");
//                close(this->sock);
//                this->sock = -1;
//                continue;
//            }

            if (connect(this->sock, addr->ai_addr, addr->ai_addrlen) < 0 && errno != EINPROGRESS) {
                perror("connect()");
                this->sock = -1;
                continue;
            }

            this->address = addr->ai_addr;
            break;
        }

        return this->sock;
    }

    void close_socket() {
        close(this->sock);
    }

    virtual ~ClientSocket() {
        freeaddrinfo(this->server_address_ll);  // Always free addrinfo memory
    }

};


class ServerSocket : Socket {


private:
    struct addrinfo *server_address_ll{};

public:
    int sock = -1;
    const std::string service;

    explicit ServerSocket(const char *serv) : Socket(serv), service(serv) {}

    int createSocket() {

        struct addrinfo addrCriteria{0};

        addrCriteria.ai_family = AF_UNSPEC;
        addrCriteria.ai_flags = AI_PASSIVE;
        addrCriteria.ai_socktype = SOCK_STREAM;
        addrCriteria.ai_protocol = IPPROTO_TCP;

        int rtnVal = getaddrinfo(nullptr, service.c_str(), &addrCriteria, &this->server_address_ll);

        if (rtnVal != 0) {
            LogErrorWithReasonX("get-addr-info () failed", gai_strerror(rtnVal));
        }

        for (struct addrinfo *addr = this->server_address_ll; addr != nullptr; addr = addr->ai_next) {
            this->sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);

            Utils::set_nonblocking_socket(this->sock);
            Utils::set_port_reusable(this->sock);


            if (this->sock < 0) {
                continue;
            }

            if (bind(this->sock, addr->ai_addr, addr->ai_addrlen) == 0 &&
                (listen(this->sock, MAX_CONNECTED_SOCKS) == 0)) {
                struct sockaddr_storage localAddr{};
                socklen_t addrSize = sizeof(localAddr);

                if (getsockname(this->sock, (struct sockaddr *) &localAddr, &addrSize) < 0) {
                    LogSystemError("get-sock-name failed!");
                }

                this->address = addr->ai_addr;

                fputs("Binding to ", stdout);
                this->printSocketAddress();
                fputc('\n', stdout);
                return this->sock;
                //break;
            }

            close(this->sock);
            this->sock = -1;
        }
        return this->sock;
    }


    int accept_new_connection() {
        struct sockaddr_storage clnt_addr{};
        size_t clnt_addr_len = sizeof(clnt_addr);
        socklen_t s_true = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &s_true, sizeof(s_true));

        while (true) {
            fd_set read_fd;
            FD_ZERO(&read_fd);
            FD_SET(this->sock, &read_fd);
            timeval interval = {10, 0};
            int isReady = select(this->sock + 1, &read_fd, nullptr, nullptr, &interval);

            if (isReady == -1) {
                LogSystemError("async failed!");
                break;
            }

            if (isReady == 0) {
                LogErrorWithReason("No incoming connection...", "");
                continue;
            }

            if (FD_ISSET(this->sock, &read_fd)) {
                int client_socks = accept(this->sock, (sockaddr *) &clnt_addr, (socklen_t *) &clnt_addr_len);

                if (client_socks < 0) {
                    return -1;
                }

                std::cout << "Handling new client" << std::endl;
                this->printSocketAddress();

                return client_socks;
            }
        }
        return -1;
    }

};


#endif //XHTTP_PACKET_H
