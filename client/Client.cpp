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
#define PROXY_HOST "127.0.0.1"
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

                    event.data.fd = client_fd;
                    event.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP;

                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) < 0) {
                        std::cerr << "epoll_ctl ADD client_fd " << client_fd << " failed: " << strerror(errno)
                                  << std::endl;
                        close(client_fd);
                        close(proxy_fd);
                        continue;
                    }
                    std::cout << "epoll_ctl ADD client_fd: " << client_fd << " successful\n";

                    event.data.fd = proxy_fd;
                    event.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP; // Add EPOLLRDHUP and EPOLLHUP
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, proxy_fd, &event) < 0) {
                        std::cerr << "epoll_ctl ADD proxy_fd " << proxy_fd << " failed: " << strerror(errno)
                                  << std::endl;
                        close(client_fd);
                        close(proxy_fd);
                        continue;
                    }
                    std::cout << "epoll_ctl ADD proxy_fd: " << proxy_fd << " successful\n";


                    client_to_proxy_map.insert(std::pair<int, int>(client_fd, proxy_fd));
                    proxy_to_client_map.insert(std::pair<int, int>(proxy_fd, client_fd));

                    if (fcntl(proxy_fd, F_GETFD) == -1 && errno == EBADF) {
                        std::cerr << "Proxy FD " << proxy_fd
                                  << " is already closed (bad file descriptor) before write! Client FD: " << client_fd
                                  << std::endl;
                        continue; // Skip the write attempt
                    }

                    std::cout << "\n--New connection accepted from client_fd: " << client_fd << ", proxy_fd: "
                              << proxy_fd << "--\n";

                } else { // Existing connection (client or proxy fd)

                    if (client_to_proxy_map.count(fd)) {
                        int proxy_fd = client_to_proxy_map.at(fd);
                        fd_to_data_map.insert(std::pair<int, std::vector<uint8_t>>(fd, std::vector<uint8_t>()));


                        Packet pck{};
                        // READ FROM LOCAL CLIENT, RAW BYTES NO FRAMING NEEDED TO DECODE....
                        ssize_t bytes_read_from_client = BufferHandler::frame_from_proxy(fd, fd_to_data_map, pck);
                        std::cout << "frame_from_proxy (client_fd: " << fd << ") returned: " << bytes_read_from_client
                                  << " bytes\n";


                        if (bytes_read_from_client > 0) {


                            /*
                             * TODO: REQUIRES FRAMING
                             */

                            // OUR SERVER ONLY UNDERSTANDS OUR PROTOCOL FUCK!

                            std::vector<uint8_t> temp = BufferHandler::encode(pck);

                            ssize_t bytes_written = BufferHandler::frame_to(temp, proxy_fd, pck.m_response_format);
                            std::cout << "frame_to_proxy (proxy_fd: " << proxy_fd << ") wrote: " << bytes_written
                                      << " bytes\n";


                            if (bytes_written > 0) {

                                std::cout << "Successfully sent from client_fd: " << fd << " to proxy_fd: " << proxy_fd
                                          << ": " << bytes_written << " bytes\n";

                            } else if (bytes_written == 0) {

                                std::cout << "Proxy fd: " << proxy_fd << " closed connection after write\n";
                                std::cout << "Cleanup and close client_fd: " << fd << " and proxy_fd: " << proxy_fd
                                          << std::endl;
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, proxy_fd, nullptr);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                                close(fd);
                                close(proxy_fd);
                                client_to_proxy_map.erase(fd);
                                proxy_to_client_map.erase(proxy_fd);

                            } else { // bytes_written < 0 (Error)

                                if (errno == EPIPE) {
                                    std::cerr << "EPIPE error writing from client_fd: " << fd << " to proxy_fd: "
                                              << proxy_fd << ". Connection broken.\n";
                                } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                    std::cerr << "Error writing from client_fd: " << fd << " to proxy_fd: " << proxy_fd
                                              << ": " << strerror(errno) << '\n';
                                }
                                std::cout << "Cleanup and close client_fd: " << fd << " and proxy_fd: " << proxy_fd
                                          << std::endl;

                            }

                        } else if (bytes_read_from_client == 0) {

                            std::cout << "Client fd: " << fd << " closed connection during read\n";
                            int proxy_fd_to_close = client_to_proxy_map[fd]; // Get proxy_fd before erasing
                            std::cout << "Cleanup and close client_fd: " << fd << " and proxy_fd: " << proxy_fd_to_close
                                      << std::endl;
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, proxy_fd_to_close, nullptr);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                            close(fd);
                            close(proxy_fd_to_close);
                            client_to_proxy_map.erase(fd);
                            proxy_to_client_map.erase(proxy_fd_to_close);


                        } else { // bytes_read_from_client < 0

                            if (errno == EAGAIN) {
                                std::cerr << "Error reading from client_fd: " << fd << ": " << strerror(errno) << '\n';
                                int proxy_fd_to_close = client_to_proxy_map[fd]; // Get proxy_fd before erasing
                                std::cout << "Cleanup and close client_fd: " << fd << " and proxy_fd: "
                                          << proxy_fd_to_close
                                          << std::endl;
                                continue;
                            }

                            std::cerr << "Error reading from client_fd: " << fd << ": " << strerror(errno) << '\n';
                            int proxy_fd_to_close = client_to_proxy_map[fd]; // Get proxy_fd before erasing
                            std::cout << "Cleanup and close client_fd: " << fd << " and proxy_fd: " << proxy_fd_to_close
                                      << std::endl;
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, proxy_fd_to_close, nullptr);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                            close(fd);
                            close(proxy_fd_to_close);
                            client_to_proxy_map.erase(fd);
                            proxy_to_client_map.erase(proxy_fd_to_close);

                        }
                        std::cout << "Continue after client_to_proxy processing\n"; // Log continue
                        continue; // Skip to next event after client processing
                    }

                    if (proxy_to_client_map.count(fd)) {

                        int client_fd = proxy_to_client_map.at(fd);
                        fd_to_data_map.insert(std::pair<int, std::vector<uint8_t>>(fd, std::vector<uint8_t>()));

                        Packet pck{};
                        ssize_t bytes_read_from_proxy = BufferHandler::frame_from_proxy(fd, fd_to_data_map, pck);
                        std::cout << "frame_from_proxy (proxy_fd: " << fd << ") returned: " << bytes_read_from_proxy
                                  << " bytes\n";


                        std::string unframed_message = BufferHandler::unframe(pck);

                        std::cout << "----PCK----" << unframed_message << std::endl;

                        if (unframed_message.empty()) {
                            std::cout << ("Unframed message is empty\n");
                           // continue;
                        }

                        try {
                            std::vector<uint8_t> buf(unframed_message.begin(), unframed_message.end());
                            pck = BufferHandler::decode(buf);
                        } catch (std::exception &e) {
                            pck.m_message = {};
                            //continue;
                        }




                        if (bytes_read_from_proxy > 0) {

                            std::cout << "--- READ FROM CLIENT: " << fd << " : " << pck.m_message << std::endl;

                            if (fcntl(fd, F_GETFD) == -1 && errno == EBADF) {
                                std::cerr << "Proxy FD " << fd
                                          << " is already closed (bad file descriptor) before write! Client FD: " << fd
                                          << std::endl;
                                continue; // Skip the write attempt
                            }


                            std::vector<uint8_t> temp = std::vector<uint8_t>(pck.m_message.begin(),
                                                                             pck.m_message.end());
                            ssize_t bytes_written = BufferHandler::frame_to_proxy(temp, client_fd);
                            std::cout << "frame_to_client (client_fd: " << client_fd << ") wrote: " << bytes_written
                                      << " bytes\n";


                            if (bytes_written > 0) {

                                std::cout << "Successfully sent from proxy_fd: " << fd << " back to client_fd: "
                                          << client_fd << ": " << bytes_written << " bytes" << std::endl;

                            } else if (bytes_written == 0) {

                                std::cout << "Proxy fd: " << fd << " closed connection after write\n";
                                std::cout << "Cleanup and close proxy_fd: " << fd << " and client_fd: " << client_fd
                                          << std::endl;
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                                close(fd);
                                close(client_fd);
                                proxy_to_client_map.erase(fd);
                                client_to_proxy_map.erase(client_fd);
                                continue; // Important: Continue to next event

                            } else { // bytes_written < 0 (Error)

                                if (errno == EPIPE) {
                                    std::cerr << "EPIPE error writing from proxy_fd: " << fd << " to client_fd: "
                                              << client_fd << ". Connection broken.\n";
                                } else if (errno != EWOULDBLOCK && errno != EAGAIN) {
                                    std::cerr << "Something weird occurred ( p->c write): " << strerror(errno) << '\n';
                                }

                                std::cout << "Cleanup and close proxy_fd: " << fd << " and client_fd: " << client_fd
                                          << std::endl;
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
//                                close(fd);
//                                close(client_fd);
//                                proxy_to_client_map.erase(fd);
//                                client_to_proxy_map.erase(client_fd);
//                                continue; // Important: Continue to next event
                            }

                        } else if (bytes_read_from_proxy == 0) {

                            std::cout << "Proxy fd: " << fd << " closed connection during read\n";
                            std::cout << "Cleanup and close proxy_fd: " << fd << " and client_fd: " << client_fd
                                      << std::endl;
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                            close(fd);
                            close(client_fd);
                            proxy_to_client_map.erase(fd);
                            client_to_proxy_map.erase(client_fd);
                            continue; // Important: Continue to next event

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