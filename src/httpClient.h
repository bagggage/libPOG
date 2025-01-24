#ifndef _HTTPCLIENT_H
#define _HTTPCLIENT_H

#include <string>

#include "dataBuffer.h"
#include "socket.h"

namespace Net {
    class HttpClient {
    private:
        Socket socket;

        std::string hostAddress;
        std::string request;
        std::string response;

        DataBuffer buffer = {};

        void Proccess();

    protected:
        std::string CreateRequest(std::string method, const std::string_view uri, const std::string_view version);

        static constexpr Address::port_t HTTP_PORT = 80;
        static constexpr Address::port_t HTTPS_PORT = 443;

    public:
        HttpClient() = default;
        virtual ~HttpClient() { HttpClient::Disconnect(); }

        Status Connect(const char* hostAddressString);
        inline void Disconnect() { socket.Close(); };

        std::string
        SendHttpRequest(const std::string_view method, const std::string_view uri, const std::string_view version);

        inline Socket::State GetState() { return socket.GetState(); }
    };
} // namespace Net

#endif
