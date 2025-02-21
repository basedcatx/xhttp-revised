#include "../includes/utilx.h"
#include "../includes/logger.h"
#include <sys/select.h>
#include <unistd.h>
#include <thread>
#include <csignal>
#include <vector>
#include <sys/epoll.h>
#include <iostream> // Included for std::cerr and std::cout

#define DEF_LOCAL_PORT "8090"
#define PROXY_HOST "170.205.31.126"
#define PROXY_PORT "8080"
#define LOCAL_HOST "0.0.0.0"

#define MAX_EVENTS 10

void cleanup();

void cleanup_handler(int signo);

void cleanup_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        cleanup();
    }
}

void close_and_clean(int client_fd, int proxy_fd, int epoll_fd, std::map<int, int> &client_to_prox_map, std::map<int, int> &proxy_to_client_map) {
    if (epoll_fd != -1) { // Check if epoll_fd is valid before using
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

    struct sigaction sa{};
    sa.sa_handler = cleanup_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGINT, &sa, nullptr) < 0 || sigaction(SIGTERM, &sa, nullptr) < 0) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    ServerSocket LocalServerManager = ServerSocket(DEF_LOCAL_PORT);
    int listen_fd = LocalServerManager.createSocket();

    struct epoll_event event{}, events[MAX_EVENTS]{};
    std::map<int, int> client_to_proxy_map{};
    std::map<int, int> proxy_to_client_map{};

    if (listen_fd < 0) {
        close(listen_fd);
        std::cout << "Probably chosen port is in use!\n";
        LogSystemError("listening_fd");
        return EXIT_FAILURE; // Exit on failure
    }

    Utils::set_nonblocking_socket(listen_fd);
    Utils::set_port_reusable(listen_fd);

    int epoll_fd = epoll_create1(0);

    if (epoll_fd < 0) {
        close(listen_fd);
        LogSystemError("epoll_create");
        return EXIT_FAILURE; // Exit on failure
    }

    event.data.fd = listen_fd;
    event.events = EPOLLIN;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event) < 0) {
        close(listen_fd);
        close(epoll_fd);
        LogSystemError("epoll_ctl");
        return EXIT_FAILURE; // Exit on failure
    }

    while (true) {

        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        for (int i = 0; i < nfds; i++) {

            if (events[i].events & EPOLLIN) { // Check for EPOLLIN event!

                int fd = events[i].data.fd;
                std::cout << "EPOLLIN event on fd: " << fd << std::endl; // Log EPOLLIN event

                if (fd == listen_fd) {

                    std::cout << "---\nListening fd " << listen_fd
                              << " received read request. Accepting new connection...\n";
                    int client_fd = LocalServerManager.accept_new_connection();
                    int proxy_fd = ClientSocket(PROXY_HOST, PROXY_PORT).createSocket();

                    if (client_fd < 0) {
                        std::cout << "Failed to establish a local connection\n";
                        continue;
                    }

                    if (proxy_fd < 0) {
                        std::cout << "Failed to establish a connection to the server. Is it even running?\n";
                        continue;
                    }

                    Utils::set_nonblocking_socket(proxy_fd);
                    Utils::set_nonblocking_socket(client_fd);
                    Utils::set_keepalive(proxy_fd);
                    Utils::set_keepalive(client_fd);

                    event.data.fd = client_fd;
                    event.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) < 0) {
                        std::cerr << "EPOLL_CTL ADD client_fd " << client_fd << " failed: " << strerror(errno)
                                  << std::endl;
                        close(client_fd);
                        close(proxy_fd);
                        continue;
                    }
                    std::cout << "EPOLL_CTL ADD client_fd: " << client_fd << " successful\n";

                    event.data.fd = proxy_fd;
                    event.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP; // Add EPOLLRDHUP and EPOLLHUP
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, proxy_fd, &event) < 0) {
                        std::cerr << "EPOLL_CTL ADD proxy_fd " << proxy_fd << " failed: " << strerror(errno)
                                  << std::endl;
                        close(client_fd);
                        close(proxy_fd);
                        continue;
                    }
                    std::cout << "EPOLL_CTL ADD proxy_fd: " << proxy_fd << " successful\n";


                    client_to_proxy_map.insert(std::pair<int, int>(client_fd, proxy_fd));
                    proxy_to_client_map.insert(std::pair<int, int>(proxy_fd, client_fd));


                    std::cout << "\n--New connection accepted connection\n";
                    std::cout << "\n-- CLIENT_FD: " << client_fd;
                    std::cout << "\n-- PROXY_FD: " << proxy_fd << std::endl;

                } else {

                    // Client to Proxy Data Handling (Local App -> Proxy)
                    if (client_to_proxy_map.count(fd)) {

                        int proxy_fd = client_to_proxy_map.at(fd);
                        std::vector<uint8_t> client_buffer; // Accumulate data here

                        while (true) {
                            uint8_t buf[CHUNK_N_BYTES];
                            ssize_t read_bytes = 0;

                            read_bytes = read(fd, buf, CHUNK_N_BYTES);

                            if (read_bytes > 0) {
                                std::cout << "Read " << read_bytes << " bytes from client fd " << fd << std::endl;
                                client_buffer.insert(client_buffer.end(), buf, buf + read_bytes); // Append to buffer
                                if (read_bytes < CHUNK_N_BYTES) { // Partial read, but data is there, continue reading if available
                                    continue; // Try to read more data immediately in the next loop iteration
                                }
                            } else if (read_bytes == 0) {
                                std::cout << "Local client (chrome-connectors) closed the connection on fd " << fd << "!\n";
                                close_and_clean(fd, proxy_fd, epoll_fd, client_to_proxy_map, proxy_to_client_map);
                                break;
                            } else { // read_bytes < 0
                                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                    std::cout << "EAGAIN or EWOULDBLOCK on client fd " << fd << ", no more data available right now\n";
                                    break; // No more data to read for now, process what we have
                                } else {
                                    std::cerr << "Error reading from client fd " << fd << ": " << strerror(errno) << std::endl;
                                    close_and_clean(fd, proxy_fd, epoll_fd, client_to_proxy_map, proxy_to_client_map);
                                    break;
                                }
                            }
                        } // End of client read while loop

                        if (!client_buffer.empty()) {
                            std::cout << "Processing accumulated " << client_buffer.size() << " bytes from client fd " << fd << std::endl;

                            Packet pck{};
                            pck.set_packet_flags(Flags::IS_REQUEST_FLAG | Flags::COMPRESSION_FLAG);
                            pck.set_message(client_buffer.data(), client_buffer.size()); // Use accumulated buffer
                            pck.set_user_acc_name("basedcatx");
                            pck.set_user_acc_pass("basedcatxpass");
                            pck.set_host("based.com");
                            pck.set_device_id("device-id");
                            pck.set_packet_id("packet-id");
                            pck.print_packet_details();

                            std::vector<uint8_t> buf_to_send = BufferHandler::encode(pck);
                            std::vector<uint8_t> framed = BufferHandler::frame(buf_to_send, const_cast<std::string &>(pck.get_http_response_format()));
                            ssize_t bytes_sent = BufferHandler::frame_to_proxy(framed, proxy_fd);

                            if (bytes_sent > 0) {
                                std::cout << "Successfully sent: " << bytes_sent << " to our remote server from client fd " << fd << std::endl;
                            } else if (bytes_sent == 0) {
                                std::cout << "For some reason, our remote server is down or closed connection (bytes_sent=0) from client fd " << fd << "\n";
                                perror("remote_server");
                                close_and_clean(fd, proxy_fd, epoll_fd, client_to_proxy_map, proxy_to_client_map);
                            } else {
                                std::cerr << "Error sending to remote server from client fd " << fd << ": " << strerror(errno) << std::endl;
                                close_and_clean(fd, proxy_fd, epoll_fd, client_to_proxy_map, proxy_to_client_map);
                            }
                        } else {
                            std::cout << "No data to process from client fd " << fd << " this time.\n";
                        }
                    }


                    // Proxy to Client Data Handling (Proxy -> Local App)
                    if (proxy_to_client_map.count(fd)) {

                        int client_fd = proxy_to_client_map[fd];
                        std::vector<uint8_t> proxy_buffer; // Accumulate data from proxy

                        while (true) {
                            uint8_t buf[CHUNK_N_BYTES];
                            ssize_t bytes_read = 0;

                            bytes_read = read(fd, buf, CHUNK_N_BYTES);

                            if (bytes_read > 0) {
                                std::cout << "Read " << bytes_read << " bytes from proxy fd " << fd << std::endl;
                                proxy_buffer.insert(proxy_buffer.end(), buf, buf + bytes_read); // Accumulate data
                                if (bytes_read < CHUNK_N_BYTES) { // Partial read but data, continue if more available
                                    continue; // Read more data immediately
                                }

                            } else if (bytes_read == 0) {
                                std::cout << "Remote proxy closed connection on fd " << fd << "!\n";
                                close_and_clean(client_fd, fd, epoll_fd, client_to_proxy_map, proxy_to_client_map); // Note the order: client_fd, proxy_fd
                                break;
                            } else { // bytes_read < 0
                                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                    std::cout << "EAGAIN or EWOULDBLOCK on proxy fd " << fd << ", no more data right now.\n";
                                    break; // No more data right now, process what we have.
                                } else {
                                    std::cerr << "Error reading from proxy fd " << fd << ": " << strerror(errno) << std::endl;
                                    close_and_clean(client_fd, fd, epoll_fd, client_to_proxy_map, proxy_to_client_map); // Note the order: client_fd, proxy_fd
                                    break;
                                }
                            }
                        } // End of proxy read while loop


                        if (!proxy_buffer.empty()) {
                            std::cout << "Processing accumulated " << proxy_buffer.size() << " bytes from proxy fd " << fd << std::endl;

                            std::vector<uint8_t> extracted_vec;
                            try {
                                std::pair extracted_pair = Utils::extract_http_frame_pay(proxy_buffer);
                                extracted_vec.assign(extracted_pair.first.begin(), extracted_pair.first.end());
                                Packet packet = BufferHandler::decode(extracted_vec);

                                std::vector<uint8_t> packet_message_vec{packet.get_message().begin(), packet.get_message().end()};
                                ssize_t bytes_sent = BufferHandler::frame_to_proxy(packet_message_vec, client_fd);

                                if (bytes_sent > 0) {
                                    std::cout << "Forwarded successfully to our client applications (Chrome) on fd " << client_fd << "\n";
                                } else if (bytes_sent == 0) {
                                    std::cout << "For some reason, our client apps closed the connection! (bytes_sent=0) on client fd " << client_fd << "\n";
                                    perror("client-apps");
                                    close_and_clean(client_fd, fd, epoll_fd, client_to_proxy_map, proxy_to_client_map); // Note the order
                                } else {
                                    std::cerr << "Error sending to client apps on client fd " << client_fd << ": " << strerror(errno) << std::endl;
                                    close_and_clean(client_fd, fd, epoll_fd, client_to_proxy_map, proxy_to_client_map); // Note order
                                }


                            } catch (std::exception &e) {
                                std::cerr << "Error decoding packet from proxy fd " << fd << ": " << e.what() << std::endl;
                                // Consider what to do when decoding fails. For now, just print error and continue, or close connection if data is crucial.
                                // For now, break and close connection as corrupted data is hard to handle in proxy scenario.
                                close_and_clean(client_fd, fd, epoll_fd, client_to_proxy_map, proxy_to_client_map); // close both if decode error
                            }

                        } else {
                            std::cout << "No data to process from proxy fd " << fd << " this time.\n";
                        }


                    }


                }


            } else if (events[i].events & (EPOLLHUP | EPOLLRDHUP)) {

                int fd = events[i].data.fd;

                if (client_to_proxy_map.count(fd)) {

                    int proxy_fd = client_to_proxy_map[fd];
                    std::cout << "Cleaning up closed client socket pair (c :::: p): " << fd << " :::: " << proxy_fd
                              << std::endl;
                    close_and_clean(fd, proxy_fd, epoll_fd, client_to_proxy_map, proxy_to_client_map);
                    break;
                }

                if (proxy_to_client_map.count(fd)) {

                    int client_fd = proxy_to_client_map[fd];
                    std::cout << "Cleaning up closed proxy socket pair (p :::: c): " << fd << " :::: " << client_fd
                              << std::endl;

                    close_and_clean(client_fd, fd, epoll_fd, client_to_proxy_map, proxy_to_client_map); // Note order
                    break;
                }

                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                close(fd);
            }
        }
    }
    cleanup();
    return EXIT_SUCCESS; // Added explicit return for success
}


[[maybe_unused]] void cleanup() {
    puts("\nCleaning up held resources!\n");
    exit(EXIT_SUCCESS);
}