#include <iostream>
#include <vector>
#include <string>
#include "../includes/packet.h"
#include "../includes/utils.h"
#include "../includes/logger.h"
#include <regex>

int main() {
//    std::string t {HTTP_TEMPLATE_BASIC};
//    std::vector<uint8_t> header{t.begin(), t.end()};
//
//
//    std::string header_str(HTTP_TEMPLATE_BASIC); // Construct std::string from vector
//    std::string m_message = "Hello world from basedcatx c";
//
//
//    Packet pck = Packet(m_message);
//    std::vector <uint8_t> encoded = BufferHandler::encode(pck);
//
//    Packet decoded = BufferHandler::decode(encoded);
//
//    std::cout << decoded.m_msg_len << '\n';
//    decoded.printPacketDetails();


    struct sockaddr_storage addr{};
    struct sockaddr_in *s = (struct sockaddr_in *) &addr;
    s->sin_family = AF_INET;
    s->sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &s->sin_addr);
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    if (connect(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    std::string request = R"(GET / HTTP/1.1\r\nHost: www.google.com\r\n)";
    //std::string request = "GET / HTTP/1.1\r\n\r\n";
    std::vector<uint8_t> request_bytes(request.begin(), request.end());
    ssize_t sent = BufferHandler::frame_to_proxy(request_bytes, sock);

    Utils::set_nonblocking_socket(sock);
    Utils::set_tcp_no_delay(sock);
    std::cout << "Sent " << sent << " bytes to proxy\n";


    fd_set read_fd;
    FD_ZERO(&read_fd);
    FD_SET(sock, &read_fd);
    struct timeval interval = {2, 0};

    while (true) {
        int activity = select(sock + 1, &read_fd, nullptr, nullptr, &interval);

        if (activity < 0 ) {
            std::cerr << "Error occurred during setup\n";
            break;
        }

        if (FD_ISSET(sock, &read_fd)) {

//            while (true) {
//
//                uint8_t buf[2048];
//                ssize_t r_bytes = read(sock, buf, 2048);
//
//                if (r_bytes == 0) {
//                    std::cout << "Connection reset";
//                    break;
//                } else if (r_bytes < 0) {
//                    std::cout << buf;
//                    break;
//                }
//
//            }

            Packet pck = BufferHandler::frame_from(sock);
            std::cout << pck.m_message;
            pck.setFlag(Flags::COMPRESSION_FLAG)->setFlag(Flags::IS_REQUEST_FLAG);

            if (pck.checkFlag(Flags::IS_RESPONSE_FLAG)) {
                std::cout << "Just wow!\n";
            }

            if (pck.checkFlag(Flags::CONNECTION_CLOSED)) {
                std::cout << "\nConnection closed\n";
                break;
            }

            std::cout << pck.m_message;
        }

     }



    return 0;
}