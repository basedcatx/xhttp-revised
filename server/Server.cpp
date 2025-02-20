#include "../includes/utilx.h"
#include "../includes/logger.h"
#include "sys/epoll.h"
#include <unistd.h>
#include <csignal>
#include <vector>
#include <iostream> // Added for std::cerr and std::cout
#include <cerrno>   // Added for errno
#include <cstring>  // Added for strerror
#include <fcntl.h>  // Added for fcntl, F_GETFD

#define DEF_LOCAL_PORT "8080"
#define PROXY_HOST "127.0.0.1"
#define PROXY_PORT "3128"
#define LOCAL_HOST "0.0.0.0"
#define MAX_EVENTS 256

void cleanup();

void cleanup_handler(int signo);


void close_and_clean(int client_fd, int proxy_fd, int epoll_fd, std::map<int, int> &client_to_prox_map,
                     std::map<int, int> &proxy_to_client_map) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, proxy_fd, nullptr);
    close(client_fd);
    close(proxy_fd);
    client_to_prox_map.erase(client_fd);
    proxy_to_client_map.erase(proxy_fd);
}


int main(int argc, char *argv[]) {

    signal(SIGPIPE, SIG_IGN); // Ignore SIGPIPE to avoid crashing on client disconnection
    ServerSocket LocalServerManager = ServerSocket(DEF_LOCAL_PORT);
    int server_sock = LocalServerManager.createSocket();
    struct epoll_event event{}, events[MAX_EVENTS];
    std::map<int, int> clients_to_proxy_socks_map{};
    std::map<int, int> proxy_socks_to_client_map{};
    std::map<int, std::vector<uint8_t>> fd_to_buf_map{};
    std::map<int, std::vector<uint8_t>> fd_to_write_buf_map{}; // Map for pending write buffers

    struct sigaction sa{};
    sa.sa_handler = cleanup_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGINT, &sa, nullptr) < 0 || sigaction(SIGTERM, &sa, nullptr) < 0) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }


    if (server_sock < 0) {
        LogSystemError("server_sock()");
        exit(EXIT_FAILURE);
    }


    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        LogSystemError("epoll_create");
    }

    // Adding server sock to read events;

    event.events = EPOLLIN;
    event.data.fd = server_sock;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_sock, &event) == -1) {
        LogSystemError("epoll_ctl add server_socks");
    }

    Utils::set_nonblocking_socket(server_sock);
    std::cout << "Server Listening....\n";

    // Let's use epoll

    while (true) {

        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        if (nfds == -1 && errno != EINTR) { // Check for EINTR, and continue if interrupted by signal
            LogSystemError("epoll_wait");
            continue; // or break, depending on desired behavior on epoll_wait error
        }
        if (nfds == -1 && errno == EINTR) {
            continue; // epoll_wait was interrupted by a signal, just continue to next loop iteration.
        }


        for (size_t i = 0; i < nfds; i++) {

            if (events[i].data.fd == server_sock) {

                int client_fd = LocalServerManager.accept_new_connection();

                if (client_fd == -1) {
                    LogErrorWithReason("accept", "epoll accept failed");
                    continue; // Continue to the next event, don't exit
                }

                ClientSocket ProxyManager = ClientSocket(PROXY_HOST, PROXY_PORT);
                int proxy_sock = ProxyManager.createSocket();

                if (proxy_sock < 0) {
                    std::cerr << "Your HTTP/HTTPS Proxy server is not running... please make sure it is!";
                    close_and_clean(client_fd, proxy_sock, epoll_fd, clients_to_proxy_socks_map, clients_to_proxy_socks_map);
                    break;
                }

                Utils::set_nonblocking_socket(proxy_sock);
                Utils::set_nonblocking_socket(client_fd);
                Utils::set_keepalive(proxy_sock);
                Utils::set_keepalive(client_fd);

                event.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP;
                event.data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                    LogSystemError("epoll_ctl add client_sock"); // Log error adding client socket
                    close(client_fd);
                    close(proxy_sock);
                    continue; // Continue to the next event
                }

                event.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP;
                event.data.fd = proxy_sock;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, proxy_sock, &event) == -1) {
                    LogSystemError("epoll_ctl add proxy_sock"); // Log error adding proxy socket
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd,
                              nullptr); // Try to remove client fd from epoll if proxy add fails
                    close(client_fd);
                    close(proxy_sock);
                    continue; // Continue to next event
                }


                clients_to_proxy_socks_map.insert(std::pair<int, int>(client_fd, proxy_sock));
                proxy_socks_to_client_map.insert(std::pair<int, int>(proxy_sock, client_fd));

                std::cout << "\n--New connection accepted from fd: " << client_fd << " proxy fd: " << proxy_sock
                          << "--\n";
                continue;
            }

            if (events[i].events & EPOLLIN) { // Handle EPOLLIN events (Read Ready)

                int fd = events[i].data.fd;

                if (clients_to_proxy_socks_map.count(fd)) {

                    int s_proxy_fd = clients_to_proxy_socks_map[fd];

                    while (true) {

                        uint8_t buf[CHUNK_N_BYTES];
                        ssize_t bytes_read = read(fd, buf, CHUNK_N_BYTES);

                        if (bytes_read > 0) {

                            std::vector<uint8_t> buf_vector(CHUNK_N_BYTES);
                            memcpy(buf_vector.data(), buf, bytes_read);
                            std::pair extracted_pair = Utils::extract_http_frame_pay(buf_vector);

                            std::vector<uint8_t> extracted_vec(extracted_pair.first.begin(),
                                                               extracted_pair.first.end());
                            Packet packet{};

                            try {

                                packet = BufferHandler::decode(extracted_vec);

                            } catch (std::exception &e) {

                                std::cerr << "Error decoding packet: " << e.what() << std::endl;
                                packet.set_packet_flags(Flags::CORRUPT_DATA);
                                continue;

                            }

                            std::vector<uint8_t> packet_message_vec{packet.get_message().begin(),
                                                                    packet.get_message().end()};

                            ssize_t bytes_sent = BufferHandler::frame_to_proxy(packet_message_vec, s_proxy_fd);

                            std::cout << "Client message: " << packet_message_vec.data() << std::endl;

                            if (bytes_sent > 0) {

                                std::cout << "Forwarded successfully to our proxy server (HTTP-handler)\n";
                                break;

                            } else if (bytes_sent == 0) {

                                std::cout << "For some reason, our proxy closed the connection!\n";
                                perror("Proxy(HTTP)");
                                close_and_clean(fd, s_proxy_fd, epoll_fd, clients_to_proxy_socks_map,
                                                clients_to_proxy_socks_map);
                                break;

                            } else {


                                if (errno == EAGAIN) {

                                    break;

                                }

                                std::cout << "For some reason, proxy connection has an issue!\n";
                                perror("Proxy(HTTP)");
                                close_and_clean(fd, s_proxy_fd, epoll_fd, clients_to_proxy_socks_map,
                                                clients_to_proxy_socks_map);
                                break;


                            } // END OF S_PROXY_FD

                        } else if (bytes_read == 0) {

                            std::cout << "The end-client closed the connection\n";
                            perror("end-client");
                            close_and_clean(fd, s_proxy_fd, epoll_fd, clients_to_proxy_socks_map,
                                            clients_to_proxy_socks_map);
                            break;

                        } else {

                            if (errno == EAGAIN) {

                                break;

                            }


                            std::cout << "The end-client acting weird\n";
                            perror("end-client");
                            close_and_clean(fd, s_proxy_fd, epoll_fd, clients_to_proxy_socks_map,
                                            clients_to_proxy_socks_map);
                            break;

                        }

                    }


                } else if (proxy_socks_to_client_map.count(fd)) {

                    int client_fd = proxy_socks_to_client_map[fd];

                    while (true) {

                        uint8_t buf[CHUNK_N_BYTES];
                        ssize_t bytes_read = read(fd, buf, CHUNK_N_BYTES);

                        if (bytes_read > 0) {

                            std::cout << "Read: " << bytes_read << " bytes"
                                      << " from local proxy(HTTP) \n";

                            Packet pck{};
                            pck.set_packet_flags(Flags::IS_RESPONSE_FLAG | Flags::COMPRESSION_FLAG);
                            pck.set_message(buf, bytes_read);
                            pck.print_packet_details();

                            /*
                             * TODO: Fill in other fields
                             */

                            std::vector<uint8_t> buf_to_send = BufferHandler::encode(pck);
                            std::vector<uint8_t> framed = BufferHandler::frame(buf_to_send,
                                                                               const_cast<std::string &>(pck.get_http_response_format()));

                            ssize_t bytes_sent = BufferHandler::frame_to_proxy(framed, client_fd);


                            if (bytes_sent > 0) {

                                std::cout << "Successfully sent a reply to our end-client: " << bytes_sent << std::endl;
                                break;

                            } else if (bytes_sent == 0) {

                                std::cout << "For some reason, our client is disconnected!\n";
                                perror("end_client");
                                close_and_clean(client_fd, fd, epoll_fd, clients_to_proxy_socks_map,
                                                proxy_socks_to_client_map);
                                break;

                            } else {

                                if (errno == EAGAIN) {

                                    break;

                                }

                                std::cout << "For some reason, our client is weird!\n";
                                perror("end_client");
                                close(fd);
                                close(client_fd);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                                break;

                            }

                        } else if (bytes_read == 0) {
                            std::cout << "Our proxy(http) closed the connection\n";
                            perror("proxy(http)");
                            close_and_clean(client_fd, fd, epoll_fd, clients_to_proxy_socks_map,
                                            proxy_socks_to_client_map);
                            break;
                        } else {

                            if (errno == EAGAIN) {

                                break;

                            }

                            close_and_clean(client_fd, fd, epoll_fd, clients_to_proxy_socks_map,
                                            proxy_socks_to_client_map);
                            break;

                        }

                    }

                }


            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) {

                int fd = events[i].data.fd;

                if (clients_to_proxy_socks_map.count(fd)) {

                    int proxy_fd = clients_to_proxy_socks_map[fd];
                    std::cout << "Cleaning up closed client socket pair (c :::: p): " << fd << " :::: " << proxy_fd
                              << std::endl;
                    clients_to_proxy_socks_map.erase(fd);
                    proxy_socks_to_client_map.erase(proxy_fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, proxy_fd, nullptr);
                    close(fd);
                    close(proxy_fd);
                    continue;

                } else if (proxy_socks_to_client_map.count(fd)) {

                    int client_fd = proxy_socks_to_client_map[fd];
                    std::cout << "Cleaning up closed proxy socket pair (p :::: c): " << fd << " :::: " << client_fd
                              << std::endl;

                    proxy_socks_to_client_map.erase(fd);
                    clients_to_proxy_socks_map.erase(client_fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                    close(fd);
                    close(client_fd);
                    continue;

                }

                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                close(fd);

            }


        }
    }


    return 0;
}


void cleanup_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        cleanup();
    }
}


void cleanup() {
    puts("\nCleaning up held resources!\n");
    exit(EXIT_FAILURE);
}