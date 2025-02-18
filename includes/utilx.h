//
// Created by BaseDCaTx on 1/16/2025.
//

#ifndef XHTTP_UTILS_H
#define XHTTP_UTILS_H

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
#include "packet.h"
#include "logger.h"
#include <vector>

#define STREAM_BUF_SIZE BUFSIZ
#define MAX_CONNECTED_SOCKS 10


class Utils {
public:
    static void str_to_uint8_vec (const std::string& str, std::vector<uint8_t> &container)  {
        for (const auto &ch : str) {
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

    static void set_port_reusable (int sock) {
        socklen_t enable = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
    }

};

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
        std::array<char, INET6_ADDRSTRLEN> addrBuff{};
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
        if (inet_ntop(address->sa_family, numericAddress, addrBuff.data(), addrBuff.size()) == nullptr) {
            std::cout << "[Invalid address]"; // Unable to convert!
        } else {
            std::cout << addrBuff.data(); // Print the address
            std::cout << "\nPort: " << port << std::endl;
        }
    }



};

class ClientSocket : Socket {

private:
    struct addrinfo *server_address_ll{};

public:
    ClientSocket(const char *host, const char *service) : Socket(host, service){}

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

            if (setsockopt(this->sock, IPPROTO_TCP, TCP_NODELAY, &tBuf, sizeof(tBuf)) < 0) {
                perror("set-sock-opt(TCP_NO-DELAY) failed");
                close(this->sock);
                this->sock = -1;
                continue;
            }

            if (connect(this->sock, addr->ai_addr, addr->ai_addrlen) < 0 && errno != EINPROGRESS) {
                LogSystemError("connect(x) failed");
                close(this->sock);
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

    explicit ServerSocket(const char *serv) : Socket(serv), service(serv)  {}

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

            if (bind(this->sock, addr->ai_addr, addr->ai_addrlen) == 0 &&  (listen(this->sock, MAX_CONNECTED_SOCKS) == 0)) {
                struct sockaddr_storage localAddr{};
                socklen_t addrSize = sizeof(localAddr);

                if (getsockname(this->sock, (struct sockaddr *) &localAddr, &addrSize) < 0) {
                    LogSystemError("get-sock-name failed!");
                }

                this->address = addr->ai_addr;

                fputs("Binding to ", stdout);
                this->printSocketAddress();
                fputc('\n', stdout);
                break;
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
                LogSystemError("select() failed!");
                break;
            }

            if (isReady == 0) {
                LogErrorWithReason("select() timed out. No incoming connection.", "");
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
    }

};






#endif //XHTTP_UTILS_H
