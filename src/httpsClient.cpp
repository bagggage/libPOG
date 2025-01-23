#include "HTTPSClient.h"

#include <iostream>

#include <openssl/bio.h>
#include <openssl/err.h>

#ifdef _WIN32
#define WIN(exp) exp
#define NIX(exp)
#else //*nix
#define WIN(exp)
#define NIX(exp) exp
#endif

using namespace Net;

HttpsClient::HttpsClient() : HttpClient() {
    sslContext = nullptr;
    sslSocket = nullptr;

    clientStatus = ClientStatus::kClientDisconnected;
}

void HttpsClient::Init(const uint32_t port, const std::string& hostAddress) {
    if ((sslContext = SSL_CTX_new(TLS_client_method())) == nullptr) {
        std::cout << "Failed to create SSL context" << std::endl;
        ERR_print_errors_fp(stderr);
        exit(SOCKET_ERROR);
    }

    if ((sslSocket = SSL_new(sslContext)) == nullptr) {
        std::cout << "Failed to create SSL socket" << std::endl;
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(sslContext);
        exit(SOCKET_ERROR);
    }

    // Connect to TCP socket
    if (!Connect(port, hostAddress)) {
        std::cout << "Failed to establish SSL connection" << std::endl;
        ERR_print_errors_fp(stderr);
        SSL_shutdown(sslSocket);
        SSL_free(sslSocket);
        SSL_CTX_free(sslContext);
        WIN(WSACleanup());
        WIN(closesocket)
        NIX(close)(clientSocket);
        freeaddrinfo(result);
        exit(SOCKET_ERROR);
    }

    // Binding an SSL socket to a TCP socket
    SSL_set_fd(sslSocket, clientSocket);

    clientStatus = ClientStatus::kCLientInited;
}

bool HttpsClient::HTTPSConnect(const uint32_t port, const std::string& hostAddress) {
    if (clientStatus != ClientStatus::kClientDisconnected) {
        std::cout << "Connection Error: client already connected " << std::endl;
        return false;
    }

    Init(port, hostAddress);

    if (SSL_connect(sslSocket) != 1) {
        std::cout << "Failed to establish SSL connection" << std::endl;
        ERR_print_errors_fp(stderr);
        return false;
    }
    clientStatus = ClientStatus::kClientConnected;

    return true;
}

std::string
HttpsClient::SendHttpsRequest(const std::string& method, const std::string& uri, const std::string& version) {
    request.clear();
    response.clear();

    request = CreateRequest(
        const_cast<std::string&>(method), const_cast<std::string&>(uri), const_cast<std::string&>(version)
    );

    Send();
    do {
        Receive();
        Proccess();
    } while (buffer.size > 0);

    return response;
}

void HttpsClient::Send() {
    if (SSL_write(sslSocket, request.c_str(), request.size()) > 0) {
        return;
    }

    std::cout << "Failed to send HTTP request" << std::endl;
    ERR_print_errors_fp(stderr);
    SSL_shutdown(sslSocket);
    WIN(WSACleanup());
    WIN(closesocket)
    NIX(close)(clientSocket);
    freeaddrinfo(result);
    SSL_free(sslSocket);
    SSL_CTX_free(sslContext);
    exit(SOCKET_ERROR);
}

void HttpsClient::Receive() {
    if ((buffer.size = SSL_read(sslSocket, buffer.data, BUFFER_MAX_SIZE)) >= 0) {
        return;
    }

    std::cout << "Receive error:" << std::endl;
    ERR_print_errors_fp(stderr);
    SSL_shutdown(sslSocket);
    WIN(WSACleanup());
    WIN(closesocket)
    NIX(close)(clientSocket);
    freeaddrinfo(result);
    SSL_free(sslSocket);
    SSL_CTX_free(sslContext);
}

void HttpsClient::Proccess() {
    response.assign(buffer.data);
}

void HttpsClient::HTTPSDisconnect() {
    if (clientStatus != ClientStatus::kClientDisconnected) {
        SSL_shutdown(sslSocket);
        SSL_free(sslSocket);
        SSL_CTX_free(sslContext);
        clientStatus = ClientStatus::kClientDisconnected;
    }
    HttpClient::Disconnect();
}
