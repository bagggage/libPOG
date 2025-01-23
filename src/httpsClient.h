#ifndef _HTTPSCLIENT_H
#define _HTTPSCLIENT_H

#include <string>

#include <openssl/ssl.h>

#include "httpClient.h"

namespace Net {
    class HttpsClient final : HttpClient {
    public:
        HttpsClient();
        ~HttpsClient() override { HttpsClient::Disconnect(); }

        bool HTTPSConnect(const uint32_t port, const std::string& hostAddress);
        std::string SendHttpsRequest(const std::string& method, const std::string& uri, const std::string& version);
        void HTTPSDisconnect();

        inline uint8_t GetClientStatus() { return clientStatus; };
        inline char* GetIpAddress() { return ipAddress; };
        inline uint32_t GetPort() { return port; };

    private:
        using HttpClient::CreateRequest;

        void Init(const uint32_t port, const std::string& hostAddress);
        void Receive();
        void Proccess();
        void Send();

    private:
        SSL_CTX* sslContext;
        SSL* sslSocket;
        uint8_t clientStatus;

        std::string request;
        std::string response;
        DataBuffer buffer;
    };
} // namespace Net

#endif // !HTTPSCLIENT_H
