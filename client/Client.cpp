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
//#define PROXY_HOST "170.205.31.126"
#define PROXY_HOST "localhost"
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
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, proxy_fd, nullptr);
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
    }

    Utils::set_nonblocking_socket(listen_fd);
    Utils::set_port_reusable(listen_fd);

    int epoll_fd = epoll_create1(0);

    if (epoll_fd < 0) {
        close(epoll_fd);
        LogSystemError("epoll_create");
    }

    event.data.fd = listen_fd;
    event.events = EPOLLIN;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event) < 0) {
        close(listen_fd);
        close(epoll_fd);
        LogSystemError("epoll_ctl");
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

                    // Existing epoll events are now registered only to client_fd and proxy fd
                    // We check if the client is ready for read, ie we know it wasn't the listening sock that called the
                    // EPOLL_EVENT, so it's definitely a client or socket....
                    // We simply check if the current fd, that has the read event is a key in our client -> buf map (client has a read event)
                    // We read from the client to our packet;
                    // Note that our client in this case, is any application connected to our local server so it
                    // spits raw HTTP Requests in plain text, so no decoding needed!

                    if (client_to_proxy_map.count(fd)) {

                        int proxy_fd = client_to_proxy_map.at(fd);

                        while (true) {

                            uint8_t buf[CHUNK_N_BYTES];
                            std::memset (buf, 0, CHUNK_N_BYTES);
                            ssize_t read_bytes = read(fd, buf, CHUNK_N_BYTES);

                            if (read_bytes > 0) {

                                std::cout << "Read: " << read_bytes << " bytes"
                                          << " from local applications like chrome\n";
                                // Process and send to the proxy

                                Packet pck{};
                                pck.set_packet_flags(Flags::IS_REQUEST_FLAG | Flags::COMPRESSION_FLAG);
                                pck.set_message(buf, read_bytes);
                                pck.set_user_acc_name("basedcatx");
                                pck.set_user_acc_pass("basedcatxpass");
                                pck.set_host("based.com");
                                pck.set_device_id("device-id");
                                pck.set_packet_id("packet-id");
                                //
                                pck.print_packet_details();

                                /*
                                 * TODO: Fill in other packet fields later, like passwords and stuff
                                 */

                                std::vector<uint8_t> buf_to_send = BufferHandler::encode(pck);

                                std::vector<uint8_t> framed = BufferHandler::frame(buf_to_send,
                                                                                   const_cast<std::string &>(pck.get_http_response_format()));

//                                std::string f = Utils::extract_http_frame_pay(framed).first;
//                                std::vector<uint8_t> f_vec(f.begin(), f.end());
//                                std::cout << '\n' << BufferHandler::decode(f_vec).get_message();

                                ssize_t bytes_sent = BufferHandler::frame_to_proxy(framed, proxy_fd);

                                if (bytes_sent > 0) {

                                    std::cout << "Successfully sent: " << bytes_sent << " to our remote server"
                                              << std::endl;
                                    break;

                                } else if (bytes_sent == 0) {

                                    std::cout << "For some reason, our remote server is down\n";
                                    perror("remote_server");
                                    close_and_clean(fd, proxy_fd, epoll_fd, client_to_proxy_map, proxy_to_client_map);
                                    break;

                                } else {

                                    if (errno == EAGAIN) {

                                       break;

                                    }

                                    std::cout << "For some reason, remote server connection has an issue!";
                                    perror("remote_server");
                                    close_and_clean(fd, proxy_fd, epoll_fd, client_to_proxy_map, proxy_to_client_map);
                                    break;
                                }


                            } else if (read_bytes == 0) {

                                std::cout << "Local client (chrome-connectors) closed the connection!\n";
                                // close_and_clean(fd, proxy_fd, epoll_fd, client_to_proxy_map, proxy_to_client_map);
                                break;

                            } else {

                                if (errno == EAGAIN) {

                                    break;

                                } else {

                                    std::cout << "Unknown error with client\n";
                                    std::cout << "Error: " << strerror(errno) << std::endl;
                                    close_and_clean(fd, proxy_fd, epoll_fd, client_to_proxy_map, proxy_to_client_map);
                                    break;

                                }
                            }

                        } // END OF WHILE LOOP

                    }


                    if (proxy_to_client_map.count(fd)) {

                        int client_fd = proxy_to_client_map[fd];

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
                                    break;

                                }

                                std::vector<uint8_t> packet_message_vec{packet.get_message().begin(),
                                                                        packet.get_message().end()};

                                ssize_t bytes_sent = BufferHandler::frame_to_proxy(packet_message_vec, client_fd);

                                if (bytes_sent > 0) {

                                    std::cout << "Forwarded successfully to our client applications (Chrome)\n";

                                } else if (bytes_sent == 0) {

                                    std::cout << "For some reason, our client apps closed the connection!";
                                    perror("client-apps");
                                    close(fd);
                                    close(client_fd);
                                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                                    break;

                                } else {

                                    if (errno == EAGAIN) {

                                        break;

                                    }

                                    std::cout << "For some reason, our client apps act weird!\n";
                                    perror("client-apps");
                                    close(fd);
                                    close(client_fd);
                                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                                    break;


                                }

                            } else if (bytes_read == 0) {

                                std::cout << "For some reason, our client apps act weird!\n";
                                perror("client-apps");
                                close(fd);
                                close(client_fd);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                                break;

                            } else {

                                if (errno == EAGAIN) break;

                                std::cout << "For some reason, our proxy act weird!\n";
                                perror("remote proxy");
                                close(fd);
                                close(client_fd);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);

                            }

                        }

                    }


                }


            } else if (events[i].events & (EPOLLHUP | EPOLLRDHUP)) {

                int fd = events[i].data.fd;

                if (client_to_proxy_map.count(fd)) {

                    int proxy_fd = client_to_proxy_map[fd];
                    std::cout << "Cleaning up closed client socket pair (c :::: p): " << fd << " :::: " << proxy_fd
                              << std::endl;
                    client_to_proxy_map.erase(fd);
                    proxy_to_client_map.erase(proxy_fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, proxy_fd, nullptr);
                    close(fd);
                    close(proxy_fd);
                    break;
                }

                if (proxy_to_client_map.count(fd)) {

                    int client_fd = proxy_to_client_map[fd];
                    std::cout << "Cleaning up closed proxy socket pair (p :::: c): " << fd << " :::: " << client_fd
                              << std::endl;

                    proxy_to_client_map.erase(fd);
                    client_to_proxy_map.erase(client_fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                    close(fd);
                    close(client_fd);
                    break;
                }

                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                close(fd);
            }
        }
    }
    cleanup();
}


[[maybe_unused]] void cleanup() {
    puts("\nCleaning up held resources!\n");
    exit(EXIT_SUCCESS);
}