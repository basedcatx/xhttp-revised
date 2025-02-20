#include <iostream>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <cerrno>

#define PROXY_PORT 3128 // Port is 3128
#define PROXY_IP "127.0.0.1"

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};
    // Now sending a valid HTTP Proxy Request:
    std::string request = "GET https://www.example.com/ HTTP/1.1\r\nHost: www.example.com\r\nConnection: close\r\n\r\n";

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return 1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PROXY_PORT);

    if (inet_pton(AF_INET, PROXY_IP, &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported" << std::endl;
        close(sock);
        return 1;
    }

    // Connect to proxy server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        std::cerr << "errno: " << errno << ", error string: " << strerror(errno) << std::endl;
        close(sock);
        return 1;
    }

    std::cout << "Connected to server at " << PROXY_IP << ":" << PROXY_PORT << std::endl;

    // Send request
    ssize_t bytes_sent = send(sock, request.c_str(), request.length(), 0);
    if (bytes_sent < 0) {
        perror("Send failed");
        close(sock);
        return 1;
    }
    std::cout << "HTTP Proxy Request sent:\n" << request << std::endl; // More descriptive output

    // Receive response
    ssize_t bytes_received;
    std::cout << "Response from server:\n";
    while ((bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        std::cout << buffer;
    }
    std::cout << std::endl;

    if (bytes_received < 0) {
        perror("Receive failed");
        std::cerr << "errno: " << errno << ", error string: " << strerror(errno) << std::endl;
    } else if (bytes_received == 0) {
        std::cout << "Connection closed by server." << std::endl;
    }

    close(sock);
    std::cout << "Connection closed." << std::endl;
    return 0;
}