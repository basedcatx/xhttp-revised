#include "../includes/utilx.h"
#include "../includes/logger.h"
#include "sys/epoll.h"
#include "../includes/packet.h"
#include <unistd.h>
#include <csignal>
#include <vector>
#include <iostream>
#include <cerrno>
#include <cstring>
#include <fcntl.h>

#define DEF_LOCAL_PORT "8080"
#define PROXY_HOST "127.0.0.1"
#define PROXY_PORT "3128"
#define LOCAL_HOST "0.0.0.0"
#define MAX_EVENTS 256

void cleanup();

void cleanup_handler(int signo);


void close_and_clean(int client_fd, int proxy_fd, int epoll_fd, std::map<int, int> &client_to_prox_map,
                     std::map<int, int> &proxy_to_client_map) {
    if (epoll_fd != -1) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, proxy_fd, nullptr);
    }
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
                    close_and_clean(client_fd, proxy_sock, epoll_fd, clients_to_proxy_socks_map, proxy_socks_to_client_map);
                    continue; // Changed break to continue to not exit the entire loop on proxy connect fail.
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

                // Client to Proxy Handling
                if (clients_to_proxy_socks_map.count(fd)) {

                    int s_proxy_fd = clients_to_proxy_socks_map[fd];
                    std::vector<uint8_t> client_buffer; // Accumulation buffer for client data

                    while (true) {
                        uint8_t buf[CHUNK_N_BYTES];
                        ssize_t bytes_read = read(fd, buf, CHUNK_N_BYTES);

                        if (bytes_read > 0) {
                            std::cout << "Read " << bytes_read << " bytes from client fd " << fd << std::endl;
                            client_buffer.insert(client_buffer.end(), buf, buf + bytes_read);
                            if (bytes_read < CHUNK_N_BYTES) {
                                continue; // Partial read, try to read more if available
                            }

                        } else if (bytes_read == 0) {
                            std::cout << "The end-client closed the connection on fd " << fd << "\n";
                            close_and_clean(fd, s_proxy_fd, epoll_fd, clients_to_proxy_socks_map, proxy_socks_to_client_map);
                            break;
                        } else { // bytes_read < 0
                            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                std::cout << "EAGAIN/EWOULDBLOCK on client fd " << fd << ", no more data right now.\n";
                                break; // No more data available right now
                            } else {
                                std::cerr << "Error reading from client fd " << fd << ": " << strerror(errno) << std::endl;
                                close_and_clean(fd, s_proxy_fd, epoll_fd, clients_to_proxy_socks_map, proxy_socks_to_client_map);
                                break;
                            }
                        }
                    } // End of client read while loop

                    if (!client_buffer.empty()) {
                        std::cout << "Processing accumulated " << client_buffer.size() << " bytes from client fd " << fd << std::endl;

                        std::vector<uint8_t> extracted_vec;
                        try {
                            std::pair extracted_pair = Utils::extract_http_frame_pay(client_buffer);
                            extracted_vec.assign(extracted_pair.first.begin(), extracted_pair.first.end());
                            Packet packet = BufferHandler::decode(extracted_vec);


                            std::vector<uint8_t> packet_message_vec{packet.get_message().begin(), packet.get_message().end()};
                            ssize_t bytes_sent = BufferHandler::frame_to_proxy(packet_message_vec, s_proxy_fd);


                            if (bytes_sent > 0) {
                                std::cout << "Forwarded successfully to our proxy server (HTTP-handler)\n";
                            } else if (bytes_sent == 0) {
                                std::cout << "For some reason, our proxy closed the connection! (bytes_sent=0)\n";
                                perror("Proxy(HTTP)");
                                close_and_clean(fd, s_proxy_fd, epoll_fd, clients_to_proxy_socks_map, proxy_socks_to_client_map);
                            } else {
                                std::cerr << "Error sending to proxy from client fd " << fd << ": " << strerror(errno) << std::endl;
                                close_and_clean(fd, s_proxy_fd, epoll_fd, clients_to_proxy_socks_map, proxy_socks_to_client_map);
                            }


                        } catch (std::exception &e) {
                            std::cerr << "Error decoding packet from client fd " << fd << ": " << e.what() << std::endl;
                            close_and_clean(fd, s_proxy_fd, epoll_fd, clients_to_proxy_socks_map, proxy_socks_to_client_map);
                        }
                    } else {
                        std::cout << "No data to process from client fd " << fd << " this time.\n";
                    }


                    // Proxy to Client Handling
                } else if (proxy_socks_to_client_map.count(fd)) {

                    int client_fd = proxy_socks_to_client_map[fd];
                    std::vector<uint8_t> proxy_buffer; // Accumulation buffer for proxy data

                    while (true) {
                        uint8_t buf[CHUNK_N_BYTES];
                        ssize_t bytes_read = read(fd, buf, CHUNK_N_BYTES);

                        if (bytes_read > 0) {
                            std::cout << "Read: " << bytes_read << " bytes from local proxy(HTTP) fd " << fd << "\n";
                            proxy_buffer.insert(proxy_buffer.end(), buf, buf + bytes_read);
                            if (bytes_read < CHUNK_N_BYTES) {
                                continue; // Partial read, try to read more if available
                            }

                        } else if (bytes_read == 0) {
                            std::cout << "Our proxy(http) closed the connection on fd " << fd << "\n";
                            close_and_clean(client_fd, fd, epoll_fd, clients_to_proxy_socks_map, proxy_socks_to_client_map);
                            break;
                        } else { // bytes_read < 0
                            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                std::cout << "EAGAIN/EWOULDBLOCK on proxy fd " << fd << ", no more data right now.\n";
                                break; // No more data right now
                            } else {
                                std::cerr << "Error reading from proxy fd " << fd << ": " << strerror(errno) << std::endl;
                                close_and_clean(client_fd, fd, epoll_fd, clients_to_proxy_socks_map, proxy_socks_to_client_map);
                                break;
                            }
                        }
                    } // End of proxy read while loop


                    if (!proxy_buffer.empty()) {
                        std::cout << "Processing accumulated " << proxy_buffer.size() << " bytes from proxy fd " << fd << "\n";

                        Packet pck{};
                        pck.set_packet_flags(Flags::IS_RESPONSE_FLAG | Flags::COMPRESSION_FLAG);
                        pck.set_message(proxy_buffer.data(), proxy_buffer.size());
                        pck.print_packet_details();

                        std::vector<uint8_t> buf_to_send = BufferHandler::encode(pck);
                        std::vector<uint8_t> framed = BufferHandler::frame(buf_to_send, const_cast<std::string &>(pck.get_http_response_format()));
                        ssize_t bytes_sent = BufferHandler::frame_to_proxy(framed, client_fd);


                        if (bytes_sent > 0) {
                            std::cout << "Successfully sent a reply to our end-client: " << bytes_sent << std::endl;
                        } else if (bytes_sent == 0) {
                            std::cout << "For some reason, our client is disconnected! (bytes_sent=0)\n";
                            perror("end_client");
                            close_and_clean(client_fd, fd, epoll_fd, clients_to_proxy_socks_map, proxy_socks_to_client_map);
                        } else {
                            std::cerr << "Error sending to client from proxy fd " << fd << ": " << strerror(errno) << std::endl;
                            close_and_clean(client_fd, fd, epoll_fd, clients_to_proxy_socks_map, proxy_socks_to_client_map);
                        }

                    } else {
                        std::cout << "No data to process from proxy fd " << fd << " this time.\n";
                    }


                }


            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) {

                int fd = events[i].data.fd;

                if (clients_to_proxy_socks_map.count(fd)) {

                    int proxy_fd = clients_to_proxy_socks_map[fd];
                    std::cout << "Cleaning up closed client socket pair (c :::: p): " << fd << " :::: " << proxy_fd
                              << std::endl;
                    close_and_clean(fd, proxy_fd, epoll_fd, clients_to_proxy_socks_map, proxy_socks_to_client_map);
                    continue;

                } else if (proxy_socks_to_client_map.count(fd)) {

                    int client_fd = proxy_socks_to_client_map[fd];
                    std::cout << "Cleaning up closed proxy socket pair (p :::: c): " << fd << " :::: " << client_fd
                              << std::endl;

                    close_and_clean(client_fd, fd, epoll_fd, clients_to_proxy_socks_map, proxy_socks_to_client_map);
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