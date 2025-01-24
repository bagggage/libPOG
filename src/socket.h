#ifndef _SOCKET_H
#define _SOCKET_H

#include <cstring>
#include <string>

#ifdef _WIN32 // Windows NT
#include <WS2tcpip.h>
#include <WinSock2.h>

#pragma comment(lib, "ws2_32.lib")
#else // POSIX
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include "dataBuffer.h"

#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif

#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif

namespace Net {
    enum Status : uint8_t {
        Success = 0,
        Failed, // Any other fail.
        AlreadyConnected = EISCONN,
        AlreadyInProgress = EALREADY,
        InvalidAddress = EAFNOSUPPORT,
        NotAvailable = EADDRNOTAVAIL,
        Refused = ECONNREFUSED,
        Timeout = ETIMEDOUT,
        TryAgain = EAGAIN,
        Unreachable = ENETUNREACH,
    };
    enum class Protocol : uint8_t {
        None = 0,
        TCP = SOCK_STREAM,
        UDP = SOCK_DGRAM
    };

    const char* GetStatusName(const Status status);
    const char* GetProtocolName(const Protocol protocol);

    /// Represents os-specific network address, used within the `Socket` to configure connections.
    /// Supports `IPv4` and `IPv6` addresses, `UNIX` local addresses supported only on *NIX systems.
    class Address {
    public:
        typedef uint16_t port_t;

        static constexpr port_t INVALID_PORT = 0;

        enum class Family : uint8_t {
            None = AF_UNSPEC,
#ifdef _WIN32
            Local = AF_INET, // There is no `AF_LOCAL/AF_UNIX` on windows.
#else
            Local = AF_LOCAL,
#endif
            IPv4 = AF_INET,
            IPv6 = AF_INET6
        };

    private:
        typedef struct sockaddr_in SOCKADDR_IN;
        typedef struct sockaddr_in6 SOCKADDR_IN6;
#ifndef _WIN32
        typedef struct sockaddr SOCKADDR;
#endif

        static constexpr uint16_t INVALID_FLAG = 0xffff;

        union {
            // Small hack to track if the os-specific value was setted.
            uint16_t _validFlag = INVALID_FLAG;

            SOCKADDR any;
            SOCKADDR_IN ipv4;
            SOCKADDR_IN6 ipv6;
        } osAddress;

        friend class Socket;

    public:
        static const char* GetFamilyName(const Family family);

        /// Construct `Address` from port and string containing IP address.
        /// - `address_str`: null-terminated string, must contain IP address.
        /// - `family`: if `None` automatically recognise address family.
        ///
        /// Returns a valid `Address` on success, to check if failed use `Address::IsValid()` on returned object.
        static Address FromString(const char* address_str, const port_t port, Family family = Family::None);

        /// Construct `Address` from port and string containing domain name, but also works for IP addresses.
        /// Can make use of DNS to translate a domain name.
        /// - `domain_str`: null-terminated string, can constain domain name or IP address.
        /// - `protocol`: target protocol, if set make garantie that returned address can be used to create connection
        /// via specified protocol.
        /// - `family`: if `None` the result address can be either `IPv4` or `IPv6`.
        ///
        /// Returns a valid `Address` on success, to check if failed use `Address::IsValid()` on returned object.
        static Address FromDomain(
            const char* domain_str,
            const port_t port,
            const Protocol protocol = Protocol::None,
            const Family family = Family::None
        );

        /// Construct `Address` that can be used for binding listening `Socket`.
        /// - `protocol`: target protocol, shouldn't be `None`.
        /// - `family`: target address family, shouldn't be `None`.
        /// - `port`: target port, `0` means any.
        ///
        /// Returns a valid `Address` if input arguments is correct.
        static Address
        MakeBind(const Protocol protocol = Protocol::TCP, const Family family = Family::IPv4, const port_t port = 0);

        /// Convert internal os-specific network address to string.
        std::string ConvertToString() const;

        inline port_t GetPort() const { return ntohs(osAddress.ipv4.sin_port); }
        inline Family GetFamily() const { return static_cast<Family>(ntohs(osAddress.ipv4.sin_family)); }

        inline bool IsValid() const { return osAddress._validFlag != INVALID_FLAG; }
        inline bool IsLocal() const {
#ifdef _WIN32
            return false;
#else
            return GetFamily() == Family::Local;
#endif
        }
        inline bool IsIPv4() const { return GetFamily() == Family::IPv4; }
        inline bool IsIPv6() const { return GetFamily() == Family::IPv6; }
    };

    class Socket {
    public:
        enum class State : uint8_t {
            None,
            Connected,
            Listening
        };

    private:
#ifndef _WIN32
        typedef int SOCKET;
#endif
        SOCKET osSocket = INVALID_SOCKET;
        State state = State::None;

        mutable Status status = Status::Success;

    public:
        Socket() noexcept = default;
        /// Opens at constructing.
        Socket(const Address::Family addrFamily, const Protocol protocol = Protocol::None) noexcept {
            Open(addrFamily, protocol);
        }
        // Move semantic.
        Socket(Socket&& other) noexcept {
            osSocket = other.osSocket;
            other.osSocket = INVALID_SOCKET;
        }
        // Socket cannot be copied.
        Socket(const Socket&) = delete;

        ~Socket() noexcept { Close(); }

        bool Open(const Address::Family addrFamily, const Protocol protocol);
        void Close();

        // Client side.
        bool Connect(const Address& address);

        /// Starts listening for incoming connections.
        /// - `address`: address to start listening at.
        /// Returns `Address::INVALID_PORT` if failed, valid port otherwise.
        Address::port_t Listen(const Address& address);
        /// Wait and accept incoming connection. Returns `Socket` connected to
        /// remote side on success, to check if the operation failed use `Socket::IsValid()` on
        /// returned object and `Socket::Fail()` on current socket to get failure code.
        Socket Accept(Address& out_remote_addr);
        Socket Accept();

        /// Sends the data to remote side. On success return the number of bytes sent.
        /// Otherwise returns `0`, use `Socket::Fail()` to determine what happend.
        uint Send(const char* data_ptr, const uint size);
        /// Receives data from remote side.
        /// Returns number of received bytes. `0` represents an error or no-data,
        /// use `Socket::Fail()` to determine what happend.
        uint Receive(char* buffer_ptr, const uint size);

        /// Same as `Send(const char*, const uint size)`, but works with typed objects.
        template<typename T>
        uint Send(const T* object) {
            return Send(reinterpret_cast<const char*>(object), sizeof(object));
        }
        /// Same as `Send(const char*, const uint size)`, but works with typed objects.
        template<typename T>
        uint Send(const T& object) {
            return Send(reinterpret_cast<const char*>(&object), sizeof(object));
        }

        /// Same as `Receive(char*, const uint size)` but works with typed objects.
        template<typename T>
        uint Receive(T* destObject) {
            return Receive(reinterpret_cast<char*>(destObject), sizeof(T));
        }
        /// Same as `Receive(char*, const uint size)` but works with typed objects.
        template<typename T>
        uint Receive(T& destObject) {
            return Receive(&destObject);
        }

        /// Returns last error/failure code and clear it.
        inline Status Fail() const {
            const Status temp = status;
            status = Success;
            return temp;
        };

        /// Returns last error/failure code without reset.
        inline Status GetStatus() const { return status; }
        inline State GetState() const { return state; };
        /// Returns `true` if socket descriptor is valid and ready to use.
        inline bool IsOpen() const { return osSocket != INVALID_SOCKET; }
        /// Returns `true` if socket connected to remote side.
        inline bool IsConnected() const { return state == State::Connected; };
        /// Returns `true` if socket is listening for connections.
        inline bool IsListening() const { return state == State::Listening; };
        /// Returns `true` if the `Socket` represents a real os-specific socket, `false` otherwise.
        inline bool IsValid() const { return IsOpen(); }
    };
} // namespace Net

#endif