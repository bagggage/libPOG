#ifndef _SOCKET_H
#define _SOCKET_H

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

#define INVALID_PORT 0

#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif

namespace Net {
    enum class Protocol : uint8_t {
        None = 0,
        TCP = SOCK_STREAM,
        UDP = SOCK_DGRAM
    };

    /// Represents os-specific network address, used within the `Socket` to configure connections.
    /// Supports `IPv4` and `IPv6` addresses, `UNIX` local addresses supported only on *NIX systems.
    class Address {
    public:
        typedef uint16_t port_t;

        static constexpr port_t invalidPort = 0;

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
#ifndef _WIN32
        typedef struct sockaddr SOCKADDR;
        typedef struct sockaddr_in SOCKADDR_IN;
        typedef struct sockaddr_in6 SOCKADDR_IN6;
#endif
        static constexpr uint16_t invalidFlag = 0xffff;

        union {
            // Small hack to track if the os-specific value was setted.
            uint16_t _validFlag = invalidFlag;

            SOCKADDR any;
            SOCKADDR_IN ipv4;
            SOCKADDR_IN6 ipv6;
        } osAddress;

        friend class Socket;

    public:
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

        inline bool IsValid() const { return osAddress._validFlag != invalidFlag; }
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
    private:
#ifndef _WIN32
        typedef int SOCKET;
#endif
        enum class State : uint8_t {
            None,
            Connected,
            Listening
        };

        SOCKET osSocket = INVALID_SOCKET;
        State status = State::None;

    public:
        Socket() noexcept = default;
        /// Opens at constructing.
        Socket(const Protocol protocol, const Address::Family addrFamily = Address::Family::None) noexcept {
            Open(protocol, addrFamily);
        }
        // Move semantic.
        Socket(Socket&& other) noexcept {
            osSocket = other.osSocket;
            other.osSocket = INVALID_SOCKET;
        }
        // Socket cannot be copied.
        Socket(const Socket&) = delete;

        ~Socket() noexcept { Close(); }

        bool Open(const Protocol protocol, const Address::Family addrFamily);
        void Close();

        // Client side.
        bool Connect(const Address& address);

        /// Starts listening for incoming connections.
        /// - `address`: address to start listening at.
        /// Returns `Address::invalidPort` if failed, valid port otherwise.
        Address::port_t Listen(const Address& address);
        /// Wait and accept incoming connection. Returns `Socket` connected to
        /// remote side on success, to check if the operation failed use `Socket::IsValid()` on
        /// returned object.
        Socket Accept(Address& out_remote_addr);
        Socket Accept();

        /// Sends the data to remote side.
        void Send(const char* data_ptr, const uint size);
        /// Receives data from remote side.
        /// Returns number of received bytes. `0` represents an error or no-data,
        /// use `Socket::Fail()` to determine what happend.
        uint Receive(char* buffer_ptr, const uint size);

        /// Same as `Send(const char*, const uint size)`, but works with typed objects.
        template<typename T>
        void Send(const T* object) {
            Send(reinterpret_cast<const char*>(object), sizeof(object));
        }
        /// Same as `Send(const char*, const uint size)`, but works with typed objects.
        template<typename T>
        void Send(const T& object) {
            Send(reinterpret_cast<const char*>(&object), sizeof(object));
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

        // Wrappers for strings
        template<>
        void Send(const char* string) {
            Send(string, std::strlen(string));
        }
        template<>
        void Send(const std::string_view& string) {
            Send(string.data(), string.size());
        }
        template<>
        void Send(const std::string& string) {
            Send(string.data(), string.size());
        }

        inline State GetState() const { return status; };
        /// Returns `true` if socket descriptor is valid and ready to use.
        inline bool IsOpen() const { return osSocket != INVALID_SOCKET; }
        /// Returns `true` if socket connected to remote side.
        inline bool IsConnected() const { return status == State::Connected; };
        /// Returns `true` if socket is listening for connections.
        inline bool IsListening() const { return status == State::Listening; };
        /// Returns `true` if the `Socket` represents a real os-specific socket, `false` otherwise.
        inline bool IsValid() const { return IsOpen(); }
    };
} // namespace Net

#endif