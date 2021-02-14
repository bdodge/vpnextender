#ifndef VPNXTCP_H_
#define VPNXTCP_H_

#include "vpnextender.h"

#if defined(Windows)
    #include <Windows.h>
    #include <rpc.h>
#elif defined(Linux)
    #include <errno.h>
    #include <unistd.h>
    #include <netdb.h>
    #include <sys/un.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netinet/tcp.h>
    #include <sys/ioctl.h>
	#include <fcntl.h>
#elif defined(OSX)
    #include <CoreFoundation/CoreFoundation.h>

    #include <errno.h>
    #include <unistd.h>
    #include <netdb.h>
    #include <sys/un.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netinet/tcp.h>
    #include <sys/ioctl.h>
    #include <sys/select.h>
	#include <fcntl.h>
#else
    unimplemented
#endif

#ifdef __cplusplus
extern "C" {
#endif

int tcp_listen_on_port(uint16_t port, SOCKET *serversock);
int tcp_accept_connection(SOCKET serversock, SOCKET *clientsock, int timeoutms);
int tcp_connect(const char *host, uint16_t port, SOCKET *socket_ptr);
int tcp_write(SOCKET sock, vpnx_io_t *io);
int tcp_read(SOCKET sock, vpnx_io_t **io);

#ifdef __cplusplus
}
#endif

#endif
