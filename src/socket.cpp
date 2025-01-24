#include "socket.h"

#include <cstring>
#include <system_error>

#include "utils.h"

using namespace Net;

#ifdef _WIN32 // Windows NT
#define OS(nt, unix) nt

typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr_in6 SOCKADDR_IN6;

static bool isWsaInitialized = false;

inline static int GetLastSystemError() {
    return WSAGetLastError();
}

static bool InitWSA() {
    WSADATA wsa_data;

    if ((int error = WSAStartup(MAKEWORD(2, 0), &wsaData)) != 0) [[unlikely]] {
        Utils::Error("Failed to init Winsock: ", std::system_category().message(error));
        return false;
    }

    return true;
}

static bool TryInitWSA() {
    if (isWsaInitialized) [[likely]] {
        return true;
    }

    isWsaInitialized = InitWSA();
    return isWsaInitialized;
}

static void DeinitWSA() {
    if (WSACleanup() != 0) [[unlikely]] {
        Utils::Error("Failed to cleanup Winsock: ", std::system_category().message(GetLastSystemError()));
        return;
    }

    isWsaInitialized = false;
}
#else // POSIX
#define OS(nt, unix) unix

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>

typedef struct addrinfo ADDRINFO;

inline static int GetLastSystemError() {
    return errno;
}
#endif

Address Address::FromString(const char* address_str, const port_t port, Family family) {
    Address result;

    if (family == Family::None) {
        // If contains `:` assume it is IPv6 address, otherwise IPv4.
        const std::string_view sv(address_str);
        family = (sv.find(':') != std::string_view::npos) ? Family::IPv6 : Family::IPv4;
    }

    if (inet_pton(static_cast<int>(family), address_str, &result.osAddress) != 1) {
        Utils::Error("Invalid address format");
        result.osAddress._validFlag = invalidFlag;
    } else {
        // Assume that `sin_port` in `sockaddr_in6` has the same byte offset.
        result.osAddress.ipv4.sin_port = htons(port);
    }
    return result;
}

Address Address::FromDomain(
    const char* domain_str,
    const port_t port,
    const Protocol protocol,
    const Family family
) {
    Address result;

    ADDRINFO hints;
    std::memset(&hints, 0, sizeof(hints));

    if (protocol != Protocol::None) hints.ai_socktype = static_cast<int>(protocol);
    if (family != Family::None) hints.ai_family = static_cast<int>(family);

    ADDRINFO* addresses = nullptr;
    int ret = getaddrinfo(domain_str, nullptr, &hints, &addresses);
    if (ret != 0) {
        Utils::Warn("Failed to get address info: ", gai_strerror(ret));
        return result;
    }

    if (!addresses) return result;

    result.osAddress.any = *addresses->ai_addr;
    freeaddrinfo(addresses);

    return result;
}

Address Address::MakeBind(
    const Protocol protocol = Protocol::TCP,
    const Family family = Family::IPv4,
    const port_t port = 0
) {
    Address result;
    if (protocol == Protocol::None || family == Family::None) {
        return result;
    }

    result.osAddress.any.sa_family = static_cast<int>(family);

    switch (family) {
        case Family::IPv4: {
            result.osAddress.ipv4.sin_addr.s_addr = INADDR_ANY;
            result.osAddress.ipv4.sin_port = htons(port);
        } break;
        case Family::IPv6: {
            std::memset(&result.osAddress.ipv6.sin6_addr, 0, sizeof(result.osAddress.ipv6.sin6_addr));
            result.osAddress.ipv6.sin6_port = htons(port);
        } break;
        default:
            break;
    }

    return result;
}

std::string Address::ConvertToString() const {
    std::string result(INET6_ADDRSTRLEN, 0);
    const void* ret;
    if (osAddress.any.sa_family == AF_INET) {
        ret = inet_ntop(AF_INET, &osAddress.ipv4.sin_addr, result.data(), result.size());
    } else {
        ret = inet_ntop(AF_INET6, &osAddress.ipv6.sin6_addr, result.data(), result.size());
    }

    if (ret == nullptr) return {};
    return std::move(result);
}

bool Socket::Open(const Protocol protocol, const Address::Family addr_family) {
    LIBPOG_ASSERT(IsOpen() == false, "Socket can be open only once");

#ifdef _WIN32
    if (!TryInitWSA()) [[unlikely]] {
        return false;
    }
#endif

    const int sock_type = protocol == Protocol::None ? SOCK_STREAM : static_cast<int>(protocol);
    int sock_prot = 0;

    switch (protocol) {
        case Protocol::TCP: sock_prot = IPPROTO_TCP; break;
        case Protocol::UDP: sock_prot = IPPROTO_UDP; break;
        case Protocol::None: break;
        default: break;
    }

    osSocket = socket(static_cast<int>(addr_family), sock_type, sock_prot);
    if (osSocket == INVALID_SOCKET) [[unlikely]] {
        Utils::Error("Failed to open socket:", std::system_category().message(GetLastSystemError()));
        return false;
    }
    return true;
}

void Socket::Close() {
    if (IsOpen() == false) [[unlikely]] return;

    status = Status::None;
    if (OS(closesocket, close)(osSocket) != 0) [[unlikely]] {
        Utils::Error("Failed to close socket:", std::system_category().message(GetLastSystemError()));
    }
}

bool Socket::Connect(const Address& address) {
    LIBPOG_ASSERT(
        (IsOpen() && status == Status::None),
        "Socket can be connected from opened state only, if it's not alredy connected or listening"
    );

    if (connect(osSocket, &address.osAddress.any, sizeof(address)) < 0) {
        Utils::Warn("Failed to connect: ", std::system_category().message(GetLastSystemError()));
        return false;
    }
    return true;
}

void Socket::Disconnect() {
    LIBPOG_ASSERT(IsConnected(), "Socket is not connected");

    Close();
}

Address::port_t Socket::Listen(const Address& address) {
    LIBPOG_ASSERT(
        (IsOpen() && status == Status::None),
        "Socket can start listening from opened state only, if it's not alredy connected or listening"
    );

    if (bind(osSocket, &address.osAddress.any, sizeof(address.osAddress.any)) == SOCKET_ERROR) {
        Utils::Error("Failed to bind address to socket: ", std::system_category().message(GetLastSystemError()));
        return Address::invalidPort;
    }

    if (listen(osSocket, 0) < 0) {
        Utils::Error("Failed to start listening: ", std::system_category().message(GetLastSystemError()));
        return Address::invalidPort;
    }
    return address.osAddress.ipv4.sin_port;
}

