#include "../src/socket.h"
#include "../src/utils.h"

#include <iostream>

int main() {
    const Net::Address serverAddress = Net::Address::FromDomain("google.com", 8080);
    std::cout << "Server address: " << serverAddress.ConvertToString() << std::endl;

    Net::Socket clientSock(Net::Address::Family::IPv4, Net::Protocol::TCP);
    LIBPOG_ASSERT(clientSock.IsOpen(), "Socket must be open");

    bool result = clientSock.Connect(serverAddress);
    LIBPOG_ASSERT(result == clientSock.IsConnected(), "Socket internal state must be the same");

    if (clientSock.IsConnected() == false) {
        std::cerr << "Socket is not connected. Exit." << std::endl;
        return -1;
    }

    std::cout << "Client connected." << std::endl;

    clientSock.Send("hello string");
    clientSock.Close();

    LIBPOG_ASSERT(!clientSock.IsConnected(), "Socket must be disconnected");
    LIBPOG_ASSERT(!clientSock.IsOpen(), "Socket must be closed");

    std::cout << "Done." << std::endl;
    return 0;
}
