#include "../includes/utils.h"
#include "../includes/logger.h"
#include "sys/epoll.h"
#include <unistd.h>
#include <csignal>
#include <vector>

#define DEF_LOCAL_PORT "8080"
#define PROXY_HOST "127.0.0.1"
#define PROXY_PORT "3128"
#define LOCAL_HOST "0.0.0.0"
#define MAX_EVENTS 20

void cleanup();

void cleanup_handler(int signo);


int main(int argc, char *argv[]) {

    signal(SIGPIPE, SIG_IGN); // Ignore SIGPIPE to avoid crashing on client disconnection
    ServerSocket LocalServerManager = ServerSocket(DEF_LOCAL_PORT);
    int server_sock = LocalServerManager.createSocket();
    struct epoll_event event{}, events[MAX_EVENTS];
    std::map<int, int> clients_to_proxy_socks_map{};
    std::map<int, int> proxy_socks_to_client_map{};
    std::map<int, std::vector<uint8_t>> fd_to_buf_map{};

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
    std::cout << "\nServer Listening....\n";

    // Let's use epoll

    while (true) {

        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        if (nfds == -1) {
            LogSystemError("epoll_wait");
        }

        for (size_t i = 0; i < nfds; i++) {

            if (events[i].data.fd == server_sock) {

                int client_fd = LocalServerManager.accept_new_connection();
                if (client_fd == -1) {
                    LogErrorWithReason("accept", "epoll accept failed");
                }

                ClientSocket ProxyManager = ClientSocket(PROXY_HOST, PROXY_PORT);
                int proxy_sock = ProxyManager.createSocket();

                Utils::set_nonblocking_socket(proxy_sock);
                Utils::set_nonblocking_socket(client_fd);

                if (proxy_sock < 0) {
                    LogSystemError("proxy_sock()");
                }

                event.events = EPOLLIN;
                event.data.fd = client_fd;

                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                    close(client_fd);
                    close(proxy_sock);
                    continue;
                }

                event.events = EPOLLIN;
                event.data.fd = proxy_sock;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, proxy_sock, &event) == -1) {
                    close(client_fd);
                    close(proxy_sock);
                    continue;
                }


                clients_to_proxy_socks_map.insert(std::pair<int, int>(client_fd, proxy_sock));
                proxy_socks_to_client_map.insert(std::pair<int, int>(proxy_sock, client_fd));


                if (fcntl(proxy_sock, F_GETFD) == -1 && errno == EBADF) {
                    std::cerr << "Proxy FD " << proxy_sock << " is already closed (bad file descriptor) before write! Client FD: " << client_fd << std::endl;
                    continue; // Skip the write attempt
                }

                std::cout << "\n--New connection accepted from fd--\n";
                continue;
            }

            if (events[i].events == EPOLLIN) {

                int fd = events[i].data.fd;

                if (clients_to_proxy_socks_map.count(fd)) {

                    int proxy_fd = clients_to_proxy_socks_map.at(fd);

                    uint8_t buf[BUFSIZ];
                    std::memset(buf, 0, BUFSIZ);
                    ssize_t read_from_fd = read(fd, buf, BUFSIZ);

                    if (read_from_fd > 0) {
                        std::cout << "--- READ FROM CLIENT: " << fd << " : " << buf << std::endl;

                        if (fcntl(proxy_fd, F_GETFD) == -1 && errno == EBADF) {
                            std::cerr << "Proxy FD " << proxy_fd << " is already closed (bad file descriptor) before write! Client FD: " << fd << std::endl;
                            continue; // Skip the write attempt
                        }

                        // Writing to proxy!
                        ssize_t proxy_sent = write(proxy_fd, buf, read_from_fd);

                        if (proxy_sent == -1) {

                            if (errno == EAGAIN || errno == EWOULDBLOCK) {

                                std::vector<uint8_t> temp;
                                temp.reserve(BUFSIZ);
                                std::memcpy(temp.data(), buf, BUFSIZ);
                                fd_to_buf_map.insert(std::pair<int, std::vector<uint8_t>>(fd, temp));
                                event.data.fd = proxy_fd;
                                event.events = EPOLLIN | EPOLLOUT;
                                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, proxy_fd, &event);

                            } else {

                                std::cout << "Something else occurred!: " << strerror(errno) << "\n";
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, proxy_fd, nullptr);
                                clients_to_proxy_socks_map.erase(fd);
                                proxy_socks_to_client_map.erase(fd);
                                close(fd);
                                close(proxy_fd);
                                continue;

                            }

                        } else if (proxy_sent == 0) {

                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, proxy_fd, nullptr);
                            clients_to_proxy_socks_map.erase(fd);
                            proxy_socks_to_client_map.erase(fd);
                            close(fd);
                            close(proxy_fd);
                            continue;

                        } else {
                            std::cout << "Successfully sent: " << proxy_sent << " to proxy\n";
                        }
                    } else {
                        std::cerr << "Something weird happened to client fd: " << fd << "\n";
                        std::cerr << "Error: " << strerror(errno) << "\n";

                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, proxy_fd, nullptr);
                        clients_to_proxy_socks_map.erase(fd);
                        proxy_socks_to_client_map.erase(fd);
                        close(fd);
                        close(proxy_fd);
                    }

                }

                if (proxy_socks_to_client_map.count(fd)) {

                    int client_fd = proxy_socks_to_client_map.at(fd);

                    uint8_t buf[BUFSIZ];
                    std::memset(buf, 0, BUFSIZ);
                    ssize_t read_from_fd = read(fd, buf, BUFSIZ);

                    if (read_from_fd > 0) {
                        std::cout << "--- READ FROM PROXY " << fd << " : " << buf << std::endl;
                        // Writing to proxy!
                        ssize_t client_sent = write(client_fd, buf, read_from_fd);

                        if (client_sent == -1) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                std::vector<uint8_t> temp;
                                temp.reserve(BUFSIZ);
                                std::memcpy(temp.data(), buf, BUFSIZ);
                                fd_to_buf_map.insert(std::pair<int, std::vector<uint8_t>>(fd, temp));
                                event.data.fd = client_fd;
                                event.events = EPOLLIN | EPOLLOUT;
                                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &event);
                            } else {
                                std::cout << "Something else occurred!\n";
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                                clients_to_proxy_socks_map.erase(fd);
                                proxy_socks_to_client_map.erase(fd);
                                close(fd);
                                close(client_fd);
                                continue;
                            }
                        } else if (client_sent == 0) {
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                            clients_to_proxy_socks_map.erase(fd);
                            proxy_socks_to_client_map.erase(fd);
                            close(fd);
                            close(client_fd);
                            continue;
                        } else {
                            std::cout << "Successfully sent: " << client_sent << " to proxy\n";
                        }
                    } else {
                        std::cerr << "Something weird happened to client fd: " << fd << "\n";
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                        clients_to_proxy_socks_map.erase(fd);
                        proxy_socks_to_client_map.erase(fd);
                        close(fd);
                        close(client_fd);
                    }
                }

            }


            if (events[i].events == EPOLLOUT) {

                int fd = events[i].data.fd;

                if (fd_to_buf_map.count(fd)) {
                    ssize_t write_bytes = write(fd, fd_to_buf_map.at(fd).data(), fd_to_buf_map.at(fd).size());

                    if (write_bytes == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            std::cout << "Fuck this shit...\n";
                        }
                    } else if (write_bytes == 0) {
                        std::cout << "Seems like write fd is closed: " << fd << '\n';
                        fd_to_buf_map.erase(fd);
                        close(fd);
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                    }

                }

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

