#include <iostream>
#include <vector>
#include <string>
#include "../includes/packet.h"
#include "../includes/utils.h"
#include <regex>

int main() {
    std::string t {HTTP_TEMPLATE_BASIC};
    std::vector<uint8_t> header{t.begin(), t.end()};


    std::string header_str(HTTP_TEMPLATE_BASIC); // Construct std::string from vector
    std::string message = "Hello world from basedcatx c";


    Packet pck = Packet(message);
    std::vector <uint8_t> encoded = BufferHandler::encode(pck);

    Packet decoded = BufferHandler::decode(encoded);

    std::cout << decoded.msgLength << '\n';
    decoded.printPacketDetails();

    return 0;
}