#include "httpClient.h"

#include <cstring>
#include <iostream>

#include "stringUtils.h"

#if _MSC_VER && !__INTEL_COMPILER
#pragma warning(disable : 4996)
#endif

using namespace Net;

Status HttpClient::Connect(const char* hostAddressStr) {
    this->hostAddress = StringUtils::ToLower(StringUtils::Trim(hostAddressStr));

    const Address hostIpAddress = Address::FromDomain(hostAddress.c_str(), HTTP_PORT);
    if (hostIpAddress.IsValid() == false) [[unlikely]] {
        return InvalidAddress;
    }

    return Success;
}

std::string
HttpClient::SendHttpRequest(const std::string_view method, const std::string_view uri, const std::string_view version) {
    response.clear();
    request.clear();
    request = CreateRequest(method.data(), uri.data(), version.data());

    socket.Send(request);
    do {
        buffer.size = socket.Receive(buffer, buffer.MAX_SIZE - 1);
        if (socket.GetStatus() != Success) {
            return response;
        }
        response.assign(buffer);
    } while (buffer.size > 0);

    return response;
}

std::string HttpClient::CreateRequest(std::string method, const std::string_view uri, const std::string_view version) {
    return request = ((method = StringUtils::ToUpper(StringUtils::Trim(method))) == "GET")
                         ? method + " " + StringUtils::Trim(uri) + "/ HTTP/" + StringUtils::Trim(version) + "\r\n" +
                               "Host:" + hostAddress + "\r\n" + "Connection: close\r\n\r\n"
                         : method + " " + StringUtils::Trim(uri) + "/ HTTP/" + StringUtils::Trim(version) + "\r\n" +
                               "Host:" + hostAddress + "\r\nConnection: close\r\n" + "Content-Type: text/html\r\n\r\n";
}