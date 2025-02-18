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
//#define PROXY_HOST "localhost"
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
    std::map<int, std::vector<uint8_t>> fd_to_data_map{};

    if (listen_fd < 0) {
        close(listen_fd);
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

        epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        for (auto ev: events) {

            if (ev.events & EPOLLIN) { // Check for EPOLLIN event!

                int fd = ev.data.fd;
                std::cout << "EPOLLIN event on fd: " << fd << std::endl; // Log EPOLLIN event

                if (fd == listen_fd) {

                    std::cout << "---\nListening fd " << listen_fd
                              << " received EPOLLIN. Accepting new connection...\n";
                    int client_fd = LocalServerManager.accept_new_connection();
                    int proxy_fd = ClientSocket(PROXY_HOST, PROXY_PORT).createSocket();
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
                    fd_to_data_map.insert(std::pair<int, std::vector<uint8_t>>(client_fd, std::vector<uint8_t>()));
                    fd_to_data_map.insert(std::pair<int, std::vector<uint8_t>>(proxy_fd, std::vector<uint8_t>()));


                    std::cout << "\n--New connection accepted connection\n";
                    std::cout << "\n-- CLIENT_FD: " << client_fd;
                    std::cout << "\n-- PROXY_FD: " << proxy_fd << std::endl;

                } else {

                    // Existing epoll events are now registered only to client_fd and proxy fd
                    // We check if the client is ready for read, ie we know it wasn't the listening sock that called the
                    // EPOLL_EVENT, so it's definitely a client or socket....
                    // We simply check if the current fd, that has the read event is a key in our client -> buf map (client has a read event)

                    if (client_to_proxy_map.count(fd)) {


                        int proxy_fd = client_to_proxy_map.at(fd);

                        // We read from the client to our packet;
                        // Note that our client in this case, is any application connected to our local server so it
                        // spits raw HTTP Requests in plain text, so no decoding needed!
                        Packet pck{};
                        ssize_t read_from_fd = BufferHandler::frame_from_proxy(fd, fd_to_data_map, pck);

                        std::cout << "frame_from (client_fd: " << fd << ") returned: " << read_from_fd
                                  << " bytes\n";

                        if (read_from_fd > 0) {

                            std::cout << "--- READ FROM CLIENT " << fd << " : " << pck.m_message << std::endl;

                            // Writing to client!
                            // We frame(frame_to), encode the raw HTTP-Like Request and forward it to our server (proxy)
                            // All handled by frame_to

                            std::vector<uint8_t> temp = BufferHandler::encode(pck);

                            ssize_t proxy_sent = BufferHandler::frame_to(temp, proxy_fd, pck.m_response_format);

                            std::cout << "frame_to_proxy (proxy_fd: " << proxy_fd << ") wrote: " << proxy_sent
                                      << " bytes\n";


                            if (proxy_sent == -1) {

                                // If our server isn't write-able, we can just continue till it is!
                                // What else can we do!

                                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                    event.data.fd = proxy_fd;
                                    event.events = EPOLLIN | EPOLLOUT;
                                    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, proxy_fd, &event);
                                    continue;

                                } else {

                                    std::cout << "Something else occurred!\n";
//                                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, proxy_fd, nullptr);
//                                    client_to_proxy_map.erase(fd);
                                    proxy_to_client_map.erase(fd);
//                                    close(fd);
                                    close(proxy_fd);

                                }

                            } else if (proxy_sent == 0) {

                                  // Remote server closed proxy port!
                                  // I feel like, let's try just believing our event can handle it (cleanup ofc)

//                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, proxy_fd, nullptr);
//                                client_to_proxy_map.erase(fd);
                                proxy_to_client_map.erase(fd);
                                close(fd);
//                                close(proxy_fd);

                            } else {
                                std::cout << "Successfully sent: " << proxy_sent << " to proxy\n";
                            }

                        } else if (read_from_fd == 0) {
                            std::cerr << "Something weird happened to proxy fd: " << fd << "\n";
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
//                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, proxy_fd, nullptr);
                            client_to_proxy_map.erase(fd);
//                            proxy_to_client_map.erase(fd);
                            close(fd);
//                            close(proxy_fd);
                        } else {

                            if (errno == EAGAIN) {
                                continue;
                            }

                            std::cerr << "Something weird happened to proxy fd: " << fd << "\n";
//                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, proxy_fd, nullptr);
//                            client_to_proxy_map.erase(fd);
                            proxy_to_client_map.erase(fd);
//                            close(fd);
                            close(proxy_fd);

                        }

                        continue;
                    }

                    if (proxy_to_client_map.count(fd)) {

                        // If FD is a key in our proxy map, we know a proxy is writable... So we get the proxy<->client pair;

                        int client_fd = proxy_to_client_map.at(fd);

                        //We try reading from the proxy, normally!
                        Packet pck{};
                        ssize_t bytes_read_from_proxy = BufferHandler::frame_from_proxy(fd, fd_to_data_map, pck);

                        std::cout << "frame_from_proxy (proxy_fd: " << fd << ") returned: " << bytes_read_from_proxy
                                  << " bytes\n";


                        // We try to un-frame it
                        std::string unframed_message = BufferHandler::unframe(pck);

                        std::cout << "----PCK----" << unframed_message << std::endl;
                        std::cout << "----PCK2----" << pck.m_message << std::endl;

                        if (unframed_message.empty()) {

                            std::cout << ("Unframed message is empty\n");
                            // continue;
                            // I'd try something later, where if this fails we just close the socket pair, cause it's useless per say
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                            client_to_proxy_map.erase(client_fd);
                            proxy_to_client_map.erase(fd);
                            close(fd);
                            close(client_fd);
                            continue;
                        }

                        try {
                            std::vector<uint8_t> buf(unframed_message.begin(), unframed_message.end());
                            pck = BufferHandler::decode(buf);
                        } catch (std::exception &e) {
                            pck.m_message = {};
                            //continue;
                            //Same here
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                            client_to_proxy_map.erase(client_fd);
                            proxy_to_client_map.erase(fd);
                            close(fd);
                            close(client_fd);
                            continue;

                            // i could throw everything in an std::runtime exception but lol
                        }


                        if (bytes_read_from_proxy > 0) {

                            std::cout << "--- READ FROM PROXY: " << fd << " : " << pck.m_message << std::endl;


                            std::vector<uint8_t> temp = std::vector<uint8_t>(pck.m_message.begin(),
                                                                             pck.m_message.end());


                            ssize_t bytes_written = BufferHandler::frame_to_proxy(temp, client_fd);

                            std::cout << "frame_to_client (client_fd: " << client_fd << ") wrote: " << bytes_written
                                      << " bytes\n";


                            if (bytes_written > 0) {

                                std::cout << "Successfully sent from proxy_fd: " << fd << " back to client_fd: "
                                          << client_fd << ": " << bytes_written << " bytes" << std::endl;

                            } else if (bytes_written == 0) {

//                                std::cout << "Proxy fd: " << fd << " closed connection after write\n";
//                                std::cout << "Cleanup and close proxy_fd: " << fd << " and client_fd: " << client_fd
//                                          << std::endl;
//                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
//                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
//                                close(fd);
//                                close(client_fd);
//                                proxy_to_client_map.erase(fd);
//                                client_to_proxy_map.erase(client_fd);
//                                continue; // Important: Continue to next event

                            } else { // bytes_written < 0 (Error)

                                if (errno == EPIPE) {
                                    std::cerr << "EPIPE error writing from proxy_fd: " << fd << " to client_fd: "
                                              << client_fd << ". Connection broken.\n";
                                } else if (errno != EWOULDBLOCK && errno != EAGAIN) {
                                    std::cerr << "Something weird occurred ( p->c write): " << strerror(errno) << '\n';
                                }

//                                std::cout << "Cleanup and close proxy_fd: " << fd << " and client_fd: " << client_fd
//                                          << std::endl;
//                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
//                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
////                                close(fd);
////                                close(client_fd);
////                                proxy_to_client_map.erase(fd);
////                                client_to_proxy_map.erase(client_fd);
////                                continue; // Important: Continue to next event
                            }

                        } else if (bytes_read_from_proxy == 0) {

                            std::cout << "Proxy fd: " << fd << " closed connection during read\n";
                            std::cout << "Cleanup and close proxy_fd: " << fd << " and client_fd: " << client_fd
                                      << std::endl;
//                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
//                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
//                            close(fd);
//                            close(client_fd);
//                            proxy_to_client_map.erase(fd);
//                            client_to_proxy_map.erase(client_fd);
//                            continue; // Important: Continue to next event

                            continue;

                        } else { // bytes_read_from_proxy < 0

                            std::cout << "Something weird occurred: (proxy read): " << strerror(errno) << '\n';
                            // Removed cleanup block from here as it might be too aggressive.
                            // Let's investigate other errors first.  If still crashing, re-add cleanup.
                            continue; // Important: Continue to next event

                        }

                    }

                }

            } else if (ev.events & (EPOLLRDHUP | EPOLLHUP)) { // Handle hang up events!
                int fd = ev.data.fd;
                std::cout << "EPOLLRDHUP or EPOLLHUP event on fd: " << fd << std::endl;
                int client_fd = -1, proxy_fd = -1;

                if (client_to_proxy_map.count(fd)) {
                    proxy_fd = client_to_proxy_map[fd];
                    client_to_proxy_map.erase(fd);
                    proxy_to_client_map.erase(proxy_fd);
                    client_fd = fd;
                } else if (proxy_to_client_map.count(fd)) {
                    client_fd = proxy_to_client_map[fd];
                    proxy_to_client_map.erase(fd);
                    client_to_proxy_map.erase(client_fd);
                    proxy_fd = fd;
                }

                fd_to_data_map.erase(fd);

                if (client_fd != -1) {
                    std::cout << "Cleanup and close fd: " << client_fd;
                    if (proxy_fd != -1) std::cout << " and proxy_fd: " << proxy_fd;
                    std::cout << std::endl;
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                    close(client_fd);
                    if (proxy_fd != -1) {
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, proxy_fd, nullptr);
                        close(proxy_fd);
                    }
                }
                continue; // Continue to next event
            }


        }


    }

    cleanup();
}


void cleanup() {
    puts("\nCleaning up held resources!\n");
    exit(EXIT_SUCCESS);
}