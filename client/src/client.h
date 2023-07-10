#ifndef CLIENT_H
#define CLIENT_H

#pragma once

#include <stdint.h>


#ifdef _WIN32 // Windows NT

#include <WS2tcpip.h>
#include <WinSock2.h>

#else  //*nix

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>

#endif

#include <cassert>
#include <iostream>

#include "bufer.h"
#include "clientStatus.h"
#include "string.h"


#ifdef _WIN32 // Windows NT

typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr_in6 SOCKADDR_IN6;

#else // POSIX

typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr_in6 SOCKADDR_IN6;
typedef struct addrinfo ADDRINFO;
typedef int SOCKET; 

#endif

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#endif

#define INVALID_PORT -1
#define SOCKET_ERROR -1

#define IPV4_LENGTH 15
#define IPV6_LENGTH 65

#define HTTP_PORT  80
#define HTTPS_PORT 443



namespace Net
{
	typedef union SocketInfo
	{
		SOCKADDR_IN* IPv4;
		SOCKADDR_IN6* IPv6;

	}SocketInfo_t;

	class Client
	{
	private:

#ifdef _WIN32
		WSADATA wsaData;
		const WORD DLLVersion = MAKEWORD(2, 2);
#endif

		SOCKET clientSocket;
		ADDRINFO hints;
		ADDRINFO* result;
		SocketInfo_t socketInfo;
		uint32_t infoLength;

		char ipAddress[IPV6_LENGTH];
		std::string hostAddress;
		uint32_t port;
		
		std::string request;
		std::string response;
		DataBuffer_t buffer;
	
		uint8_t clientStatus;
		
	public:
		Client();
		~Client();

		void Connect(const uint32_t port, const std::string& hostAddress);
		std::string SendHttpRequest(const std::string& method, const std::string& uri, const std::string& version);
		void Disconnect();

		uint8_t GetClientStatus();
		char* GetIpAddress();
		uint32_t GetPort();

	private:
		void Init(const uint32_t port, const std::string& hostAddress);
		inline std::string CreateRequest(std::string& method, std::string& uri, std::string& version);
		void Receive();
		void Proccess();
		void Send();
	};



}

#endif // !CLIENT_H
