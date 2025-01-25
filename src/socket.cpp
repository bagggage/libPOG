#include "socket.h"

#include <cstring>
#include <system_error>

#include "utils.h"

using namespace Net;

#ifdef _WIN32 // Windows NT
#define OS(nt, unix) nt

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
#include <poll.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>

inline static int GetLastSystemError() {
    return errno;
}
#endif

const char* GetStatusName(const Status status) {
    switch (status) {
        case Status::Success:
            return "Success";
        case Status::Failed:
            return "Failed";
        case Status::ConnectionRefused:
            return "Connection Refused";
        case Status::AlreadyConnected:
            return "Already Connected";
        case Status::AlreadyInProgress:
            return "Already In Progress";
        case Status::InvalidAddress:
            return "Invalid Address";
        case Status::NotAvailable:
            return "Not Available";
        case Status::Timeout:
            return "Timeout";
        case Status::TryAgain:
            return "Try Again";
        case Status::Unreachable:
            return "Unreachable";
        default:
            break;
    }
    return "Unknown";
}

const char* GetProtocolName(const Protocol protocol) {
    switch (protocol) {
        case Protocol::None:
            return "None";
        case Protocol::TCP:
            return "TCP";
        case Protocol::UDP:
            return "UDP";
        default:
            break;
    }
    return "Unknown";
}

const char* Address::GetFamilyName(const Family family) {
    switch (family) {
        case Family::None:
            return "None";
#ifndef _WIN32
        case Family::Local:
            return "Local";
#endif
        case Family::IPv4:
            return "IPv4";
        case Family::IPv6:
            return "IPv6";
        default:
            break;
    }
    return "Unknown";
}

Address Address::FromString(const char* addressStr, const port_t port, Family family) {
    Address result;

    if (family == Family::None) {
        // If contains `:` assume it is IPv6 address, otherwise IPv4.
        const std::string_view sv(addressStr);
        family = (sv.find(':') != std::string_view::npos) ? Family::IPv6 : Family::IPv4;
    }

    int ret;
    if (family == Family::IPv4) {
        ret = inet_pton(static_cast<int>(family), addressStr, &result.osAddress.ipv4.sin_addr);
    } else {
        ret = inet_pton(static_cast<int>(family), addressStr, &result.osAddress.ipv6.sin6_addr);
    }

    if (ret != 1) {
        Utils::Error("Invalid address format");
        result.osAddress._validFlag = INVALID_FLAG;
    } else {
        result.osAddress.any.sa_family = static_cast<int>(family);
        // Assume that `sin_port` in `sockaddr_in6` has the same byte offset.
        result.osAddress.ipv4.sin_port = htons(port);
    }
    return result;
}

Address Address::FromDomain(const char* domainStr, const port_t port, const Protocol protocol, const Family family) {
    typedef struct addrinfo ADDRINFO;

    Address result;

    ADDRINFO hints;
    std::memset(&hints, 0, sizeof(hints));

    if (protocol != Protocol::None) {
        hints.ai_socktype = static_cast<int>(protocol);
    }
    if (family != Family::None) {
        hints.ai_family = static_cast<int>(family);
    }

    ADDRINFO* addresses = nullptr;
    int ret = getaddrinfo(domainStr, nullptr, &hints, &addresses);
    if (ret != 0) {
        Utils::Warn("Failed to get address info: ", gai_strerror(ret));
        return result;
    }

    if (!addresses) {
        return result;
    }

    result.osAddress.any = *addresses->ai_addr;
    result.osAddress.ipv4.sin_port = htons(port);
    freeaddrinfo(addresses);

    return result;
}

Address Address::MakeBind(const Protocol protocol, const Family family, const port_t port) {
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
    std::string result;
    const void* ret;
    if (osAddress.any.sa_family == AF_INET) {
        result.reserve(INET_ADDRSTRLEN + 6);
        result.resize(INET_ADDRSTRLEN);
        ret = inet_ntop(AF_INET, &osAddress.ipv4.sin_addr, result.data(), result.size());
    } else {
        result.reserve(INET6_ADDRSTRLEN + 6);
        result.resize(INET6_ADDRSTRLEN);
        ret = inet_ntop(AF_INET6, &osAddress.ipv6.sin6_addr, result.data(), result.size());
    }

    if (ret == nullptr) {
        return {};
    }

    const size_t strLength = std::strlen(result.c_str());
    result.resize(strLength + 1);

    result[strLength] = ':';
    result += std::to_string(GetPort());

    return std::move(result);
}

bool Socket::Open(const Address::Family addr_family, const Protocol protocol) {
    LIBPOG_ASSERT(IsOpen() == false, "Socket can be open only once");

#ifdef _WIN32
    if (!TryInitWSA()) [[unlikely]] {
        status = Failed;
        return false;
    }
#endif

    const int sock_type = protocol == Protocol::None ? SOCK_STREAM : static_cast<int>(protocol);
    int sock_prot = 0;

    switch (protocol) {
        case Protocol::TCP:
            sock_prot = IPPROTO_TCP;
            break;
        case Protocol::UDP:
            sock_prot = IPPROTO_UDP;
            break;
        case Protocol::None:
            break;
        default:
            break;
    }

    osSocket = socket(static_cast<int>(addr_family), sock_type, sock_prot);
    if (osSocket == INVALID_SOCKET) [[unlikely]] {
        status = static_cast<Status>(GetLastSystemError());
        Utils::Error("Failed to open socket: ", std::system_category().message(static_cast<int>(status)));
        return false;
    }

    return true;
}

void Socket::Close() {
    if (IsOpen() == false) [[unlikely]] {
        return;
    }

    state = State::None;
    if (OS(closesocket, close)(osSocket) != 0) [[unlikely]] {
        status = static_cast<Status>(GetLastSystemError());
        Utils::Error("Failed to close socket: ", std::system_category().message(static_cast<int>(status)));
    }

    osSocket = INVALID_SOCKET;
}

bool Socket::Connect(const Address& address) {
    LIBPOG_ASSERT(
        (IsOpen() && state == State::None),
        "Socket can be connected from opened state only, if it's not alredy connected or listening"
    );

    if (connect(osSocket, &address.osAddress.any, sizeof(address)) < 0) {
        status = static_cast<Status>(GetLastSystemError());
        Utils::Error("Failed to connect: ", std::system_category().message(static_cast<int>(status)));
        return false;
    }

    state = State::Connected;
    return true;
}

Address::port_t Socket::Listen(const Address& address) {
    LIBPOG_ASSERT(
        (IsOpen() && state == State::None),
        "Socket can start listening from opened state only, if it's not alredy connected or listening"
    );

    if (bind(osSocket, &address.osAddress.any, sizeof(address.osAddress.any)) < 0) {
        status = static_cast<Status>(GetLastSystemError());
        Utils::Error("Failed to bind address to socket: ", std::system_category().message(static_cast<int>(status)));
        return Address::INVALID_PORT;
    }
    if (listen(osSocket, 0) < 0) {
        status = static_cast<Status>(GetLastSystemError());
        Utils::Error("Failed to start listening: ", std::system_category().message(static_cast<int>(status)));
        return Address::INVALID_PORT;
    }

    state = State::Listening;
    return address.GetPort();
}

Socket Socket::Accept(Address& outRemoteAddress) {
    LIBPOG_ASSERT(IsListening(), "Socket must listen");

    Socket result;
    socklen_t sockSize = sizeof(outRemoteAddress.osAddress.any);

    result.osSocket = accept(osSocket, &outRemoteAddress.osAddress.any, &sockSize);
    if (result.osSocket == INVALID_SOCKET) [[unlikely]] {
        status = static_cast<Status>(GetLastSystemError());
        return result;
    }

    result.state = State::Connected;
    return result;
}

uint Socket::Send(const char* data, const uint size) {
    LIBPOG_ASSERT(IsConnected(), "Socket must be connected");

    ssize_t ret = send(osSocket, data, size, 0);
    if (ret < 0) [[unlikely]] {
        status = static_cast<Status>(GetLastSystemError());
        return 0;
    }

    return static_cast<uint>(ret);
}

uint Socket::Receive(char* buffer, const uint size) {
    LIBPOG_ASSERT(IsConnected(), "Socket must be connected");

    const ssize_t ret = recv(osSocket, buffer, size, 0);
    if (ret < 0) [[unlikely]] {
        status = static_cast<Status>(GetLastSystemError());
        return 0;
    }

    return static_cast<uint>(ret);
}

uint Socket::SendTo(const Address& address, const char* dataPtr, const uint size) {
    LIBPOG_ASSERT(IsOpen(), "Socket must be open");

    const ssize_t ret = sendto(osSocket, dataPtr, size, 0, &address.osAddress.any, sizeof(address.osAddress.any));
    if (ret < 0) [[unlikely]] {
        status = static_cast<Status>(GetLastSystemError());
        return 0;
    }

    return static_cast<uint>(ret);
}

uint Socket::ReceiveFrom(char* bufferPtr, const uint size, Address& outRemoteAddress) {
    LIBPOG_ASSERT(IsOpen(), "Socket must be open");

    socklen_t sockSize = sizeof(outRemoteAddress.osAddress.any);
    const ssize_t ret = recvfrom(osSocket, bufferPtr, size, 0, &outRemoteAddress.osAddress.any, &sockSize);
    if (ret < 0) {
        status = static_cast<Status>(GetLastSystemError());
        return 0;
    }

    return static_cast<uint>(ret);
}

uint Socket::ReceiveFrom(char* bufferPtr, const uint size, Socket& outSocket) {
    LIBPOG_ASSERT(outSocket.IsOpen() == false, "Output socket must be closed");

    Address remoteAddress;
    const uint ret = ReceiveFrom(bufferPtr, size, remoteAddress);
    if (remoteAddress.IsValid() == false) [[unlikely]] {
        return ret;
    }

    if (outSocket.Open(remoteAddress.GetFamily(), Net::Protocol::UDP) == false) [[unlikely]] {
        return ret;
    }

    outSocket.Connect(remoteAddress);
    return ret;
}

// Wrappers for strings
template<>
uint Socket::Send(const char* string) {
    return Send(string, std::strlen(string));
}
template<>
uint Socket::Send(const std::string_view& string) {
    return Send(string.data(), string.size());
}
template<>
uint Socket::Send(const std::string& string) {
    return Send(string.data(), string.size());
}

bool Socket::SetOption(const Option option, const void* value, const uint valueSize) {
    if (setsockopt(osSocket, SOL_SOCKET, static_cast<int>(option), value, sizeof(value)) == SOCKET_ERROR) {
        status = static_cast<Status>(GetLastSystemError());
        return false;
    }
    return true;
}

bool Socket::GetOption(const Option option, void* outValue, uint& valueSize) const {
    if (getsockopt(osSocket, SOL_SOCKET, static_cast<int>(option), &outValue, &valueSize) == SOCKET_ERROR) {
        status = static_cast<Status>(GetLastSystemError());
        return false;
    }
    return true;
}