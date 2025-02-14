#include "../includes/utils.h"
#include "../includes/logger.h"
#include <sys/select.h>
#include <unistd.h>
#include <thread>
#include <csignal>
#include <vector>

#define DEF_LOCAL_PORT "8090"
#define PROXY_HOST "127.0.0.1"
#define PROXY_PORT "8080"
#define LOCAL_HOST "localhost"

void cleanup();
void cleanup_handler(int signo);
void handle_client_thread(int sock);
const std::string HEADER = HTTP_TEMPLATE_BASIC;
int local_listening_server_socket = -1;
int remote_proxy_socket = -1;


int main(int argc, char *argv[]) {

    signal(SIGPIPE, SIG_IGN); // Ignore SIGPIPE to avoid crashing on client disconnection
    ServerSocket LocalServerManager = ServerSocket(DEF_LOCAL_PORT);
    int server_sock = LocalServerManager.createSocket();
    local_listening_server_socket = server_sock;

    if (server_sock < 0) {
        LogSystemError("server_sock()");
        exit(EXIT_FAILURE);
    }

    struct sigaction sa{};
    sa.sa_handler = cleanup_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGINT, &sa, nullptr) < 0 || sigaction(SIGTERM, &sa, nullptr) < 0) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    fd_set read_fds;

    while (true) {

        FD_ZERO(&read_fds);
        FD_SET(server_sock, &read_fds);

        int activity = select(server_sock + 1, &read_fds, nullptr, nullptr, nullptr);

        if (activity < 0 && errno != EINTR) {
            perror("select");
            break;
        }

        if (FD_ISSET(server_sock, &read_fds)) {

            int client_sock = LocalServerManager.accept_new_connection(); // Sets server_sock to listen, accept mode...

            if (client_sock < 0) {
                perror("AcceptTCPConnection");
                continue;
            }


            std::thread client_thread(handle_client_thread, client_sock);
            client_thread.detach();


        }
    }

    cleanup();
    return 0;
}


void handle_client_thread(int socks) {
    ClientSocket proxy = ClientSocket(PROXY_HOST, PROXY_PORT);
    int proxy_soc = proxy.createSocket();
    ClientSocket local_client = ClientSocket(LOCAL_HOST, DEF_LOCAL_PORT);
    int local_soc = local_client.createSocket();
    remote_proxy_socket = proxy_soc;

    if (proxy_soc < 0) {
        LogSystemError("Proxy socket");
        close(proxy_soc);
    }

    if (local_soc < 0) {
        LogSystemError("Local client error");
        close(local_soc);
    }


    Utils::set_nonblocking_socket(proxy_soc);
    Utils::set_nonblocking_socket(local_soc);

    socks = local_soc;

    std::cout << "New client connected: " << local_soc  << '\n';

    while (true) {

        fd_set read_fd_set, write_fd_set;
        FD_ZERO(&read_fd_set);
        FD_ZERO(&write_fd_set);

        FD_SET(local_soc, &read_fd_set);
        FD_SET(proxy_soc, &read_fd_set);

        int max_fd = (local_soc > proxy_soc ? local_soc : proxy_soc) + 1;

        struct timeval timeout = {5, 0}; // 5-second timeout for select
        int activity = select(max_fd, &read_fd_set, &write_fd_set, nullptr, &timeout);

        if (activity < 0 && errno != EINTR) {
            LogSystemError("Client activity");
            break;
        }


        if (activity == 0) {
            // Timeout
            continue;
        }

        if (FD_ISSET(socks, &read_fd_set)) {

            Packet packet = BufferHandler::decode(BufferHandler::frame_from(socks));

            if (!packet.message.empty()) {
                std::vector<uint8_t> data_to_send = BufferHandler::encode(packet);

                if (!data_to_send.empty()) {

                    ssize_t proxy_sent = BufferHandler::frame_to(data_to_send, proxy_soc,
                                                                 const_cast<std::string &>(HEADER));

                    if (proxy_sent < 0 && errno != EAGAIN) {
                        perror("Error writing to proxy");
                        break;
                    }


                    printf("Sent %zd bytes to server\n\n", proxy_sent);
                }

            }

        }


        // Handle data from proxy to client
        if (FD_ISSET(proxy_soc, &read_fd_set)) {

            Packet pck = BufferHandler::decode(BufferHandler::frame_from(proxy_soc));

            if (!pck.message.empty()) pck.printPacketDetails();

            std::vector<uint8_t> data_to_send = BufferHandler::encode(pck);

            if (!data_to_send.empty()) {
                BufferHandler::frame_to(data_to_send, socks, const_cast<std::string &>(HEADER));
            }
        }

    }



    close(socks);
    close(local_listening_server_socket);
    close(proxy_soc);
    printf("Thread exiting for client %d.\n", socks);
}



void cleanup_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        cleanup();
    }
}


void cleanup() {
    puts("\nCleaning up held resources!\n");
    if (local_listening_server_socket >= 0) {
        close(local_listening_server_socket);
    }
    if (remote_proxy_socket >= 0) {
        close(remote_proxy_socket);
    }
    exit(EXIT_FAILURE);
}

