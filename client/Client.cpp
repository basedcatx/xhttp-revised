#include "../includes/utils.h"
#include "../includes/logger.h"
#include <sys/select.h>
#include <unistd.h>
#include <thread>
#include <csignal>
#include <vector>
#include <sys/epoll.h>

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

            if (ev.events == EPOLLIN) {

                int fd = ev.data.fd;

                if (fd == listen_fd) {

                    int client_fd = LocalServerManager.accept_new_connection();
                    int proxy_fd = ClientSocket(PROXY_HOST, PROXY_PORT).createSocket();
                    Utils::set_nonblocking_socket(proxy_fd);
                    Utils::set_nonblocking_socket(client_fd);

                    event.data.fd = client_fd;
                    event.events = EPOLLIN;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) < 0) {
                        close(client_fd);
                        close(proxy_fd);
                        continue;
                    }

                    event.data.fd = proxy_fd;
                    event.events = EPOLLIN;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, proxy_fd, &event) < 0) {
                        close(client_fd);
                        close(proxy_fd);
                        continue;
                    }

                    client_to_proxy_map.insert(std::pair<int, int>(client_fd, proxy_fd));
                    proxy_to_client_map.insert(std::pair<int, int>(proxy_fd, client_fd));

                    if (fcntl(proxy_fd, F_GETFD) == -1 && errno == EBADF) {
                        std::cerr << "Proxy FD " << proxy_fd
                                  << " is already closed (bad file descriptor) before write! Client FD: " << client_fd
                                  << std::endl;
                        continue; // Skip the write attempt
                    }

                    std::cout << "\n--New connection accepted from fd--\n";

                } else {

                    if (client_to_proxy_map.count(fd)) {
                        int proxy_fd = client_to_proxy_map.at(fd);
                        uint8_t buf[BUFSIZ];

                        ssize_t bytes_read_from_client = read(fd, buf, BUFSIZ);

                        if (bytes_read_from_client > 0) {
                            ssize_t bytes_written = write(proxy_fd, buf, bytes_read_from_client);

                            if (bytes_written > 0) {

                                std::cout << "Successfully sent from client to proxy: " << bytes_written << "\n";

                            } else if (bytes_written == 0) {

                                std::cout << "Proxy closed connection\n";
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, proxy_fd, nullptr);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                                close(fd);
                                close(proxy_fd);
                                client_to_proxy_map.erase(fd);
                                client_to_proxy_map.erase(proxy_fd);

                            } else {
                                std::cout << "Something weird occurred ( c->p write): " << strerror(errno) << '\n';
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, proxy_fd, nullptr);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                                close(fd);
                                close(proxy_fd);
                                client_to_proxy_map.erase(fd);
                                client_to_proxy_map.erase(proxy_fd);
                            }

                        } else if (bytes_read_from_client == 0) {

                            std::cout << "Client closed connection\n";
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, proxy_fd, nullptr);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                            close(fd);
                            close(proxy_fd);
                            client_to_proxy_map.erase(fd);
                            client_to_proxy_map.erase(proxy_fd);

                        } else {

                            std::cout << "Something weird occurred: (client read)" << strerror(errno) << '\n';
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, proxy_fd, nullptr);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                            close(fd);
                            close(proxy_fd);
                            client_to_proxy_map.erase(fd);
                            client_to_proxy_map.erase(proxy_fd);

                        }
                    }

                    if (proxy_to_client_map.count(fd)) {
                        int client_fd = proxy_to_client_map.at(fd);
                        uint8_t buf[BUFSIZ];

                        ssize_t bytes_read_from_proxy = read(fd, buf, BUFSIZ);

                        if (bytes_read_from_proxy > 0) {
                            ssize_t bytes_written = write(client_fd, buf, bytes_read_from_proxy);

                            if (bytes_written > 0) {
                                std::cout << "Successfully sent from server back to client" << bytes_written
                                          << std::endl;
                            } else {
                                if (errno != EWOULDBLOCK || errno != EAGAIN) {
                                    std::cout << "Something weird occurred: (proxy read): " << strerror(errno) << '\n';
                                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                                    close(fd);
                                    close(client_fd);
                                    client_to_proxy_map.erase(fd);
                                    client_to_proxy_map.erase(client_fd);
                                }
                            }

                        } else {
                            if (errno != EWOULDBLOCK || errno != EAGAIN) {
                                std::cout << "Something weird occurred: (proxy read)" << strerror(errno) << '\n';
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                                close(fd);
                                close(client_fd);
                                client_to_proxy_map.erase(fd);
                                client_to_proxy_map.erase(client_fd);
                            }
                        }

                    }

                }

            }

        }

    }
}


void cleanup() {
    puts("\nCleaning up held resources!\n");
    exit(EXIT_SUCCESS);
}

