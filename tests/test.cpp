#include <iostream>
#include <vector>
#include <string>
#include "../includes/packet.h"
#include "../includes/utilx.h"
#include "../includes/logger.h"
#include <regex>

int main() {

//    Base64 b64;
//    std::vector<uint8_t> buf{'h', 'e', 'e'};
//    std::cout << b64.base64_encode(buf);

    std::string h{"HTTP/1.1\r\n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/r/n"};

    Packet pck;
    pck.m_message = h;
    std::cout << BufferHandler::unframe(pck);


    std::regex pattern{R"(/r/n(.*?)/r/n)"};
    std::smatch matcher;

    if (std::regex_search(pck.m_message, matcher, pattern)) {
        std::cout << "Found: " << matcher.size() << " matches\n" << matcher[0] << "\n";
        std::cout << matcher[0];
        std::cout << "\n---------------------------------------------------\n";
        std::cout << matcher[1];
    }

    return 0;
}