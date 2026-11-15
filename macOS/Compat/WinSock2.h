// Winsock surface used by the debugger servers, mapped onto BSD sockets.
#pragma once

#include "windows.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>

typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define SD_RECEIVE SHUT_RD
#define SD_SEND SHUT_WR
#define SD_BOTH SHUT_RDWR
#define WSAECONNRESET ECONNRESET
#define WSAEWOULDBLOCK EWOULDBLOCK
#define MAKEWORD(lo, hi) ((WORD)(((BYTE)(lo)) | (((WORD)((BYTE)(hi))) << 8)))

struct WSADATA {
    WORD wVersion;
    WORD wHighVersion;
};

inline int WSAStartup(WORD version, WSADATA* data)
{
    if (data) { data->wVersion = version; data->wHighVersion = version; }
    return 0;
}
inline int WSACleanup() { return 0; }
inline int WSAGetLastError() { return errno; }
inline int closesocket(SOCKET s) { return ::close(s); }
