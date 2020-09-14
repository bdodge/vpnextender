
#include "vpnxtcp.h"
#include "vpnextender.h"

int tcp_listen_on_port(uint16_t port, SOCKET *socket_ptr)
{
    struct sockaddr_in serv_addr;
    #if 0
    #ifdef Windows
    unsigned long nonblock;
    #else
    uint32_t nonblock;
    #endif
    #endif
    int enable;
    int result;
    SOCKET sock;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        fprintf(stderr, "Can't create server socket\n");
        return INVALID_SOCKET;
    }
    
    enable = 1;
    result = setsockopt(
                        sock,
                        SOL_SOCKET,
                        SO_REUSEADDR,
                        (char*)&enable,
                        sizeof(enable)
                      );
    if (result < 0)
    {
        return result;
    }
    #if 0
    nonblock = 1;
    result = ioctl_socket(sock, FIONBIO, &nonblock);
    if (result < 0)
    {
        return result;
    }
    #endif
    memset((char *)&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    result = bind(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (result < 0)
    {
        fprintf(stderr, "Can't bind server socket to port %u\n", port);
        closesocket(sock);
        return result;
    }
    result = listen(sock, 1);
    if (result < 0)
    {
        closesocket(sock);
        fprintf(stderr, "Can't listen on port\n");
    }
    *socket_ptr = sock;
    return result;
}

int tcp_accept_connection(SOCKET serversock, SOCKET *clientsock_ptr)
{
    struct sockaddr_in cli_addr;
    socklen_t clilen;
    SOCKET clientsock;

    clilen = sizeof(cli_addr);
    clientsock = accept(serversock, (struct sockaddr *)&cli_addr, &clilen);

    if (clientsock != INVALID_SOCKET)
    {
        #ifdef Windows
        unsigned long nonblock;
        #else
        uint32_t nonblock;
        #endif
        nonblock = 1;
        if (ioctlsocket(clientsock, FIONBIO, &nonblock) < 0)
        {
            fprintf(stderr, "Can't make client socket non-blocking\n");
            closesocket(clientsock);
            return -1;
        }
		*clientsock_ptr = clientsock;
	    return 0;
    }
	fprintf(stderr, "Accept failed\n");
	return -1;
}

int tcp_write(SOCKET sock, vpnx_io_t *io)
{
    int sent;
    int wc, sv;
    fd_set  wfds;
    struct  timeval timeout;
    
    int waitms = 15000;
    
    sent = 0;
    do
    {
        FD_ZERO (&wfds);
        FD_SET  (sock, &wfds);
    
        timeout.tv_sec  = waitms / 1000;
        timeout.tv_usec = (waitms - ((waitms / 1000) * 1000)) * 1000;
    
        sv = select(sock + 1, NULL, &wfds, NULL, &timeout);
        if (sv < 0)
        {
            fprintf(stderr, "TCP connection broke\n");
            return -1;
        }
        if (sv == 0)
        {
            fprintf(stderr, "TCP connection blocked\n");
            return -1;
        }
        wc = (int)send(sock, (char*)io->bytes + sent, (size_t)(io->count - sent), 0);
        if (wc < 0)
        {
#ifdef Windows
            fprintf(stderr, "TCP write err=%d\n", WSAGetLastError());
#else
            fprintf(stderr, "TCP write err=%d\n", errno);
#endif
            return wc;
        }
        sent += wc;
    }
    while (sent < io->count);
    
    return 0;
}

int tcp_read(SOCKET sock, vpnx_io_t **io)
{
	static vpnx_io_t s_io;
    int rc, sv;
    fd_set  rfds;
    struct  timeval timeout;
    
	*io = NULL;
	
    // short wait for read, to keep checking usb
    int waitms = 10;
    
    FD_ZERO (&rfds);
    FD_SET  (sock, &rfds);

    timeout.tv_sec  = waitms / 1000;
    timeout.tv_usec = (waitms - ((waitms / 1000) * 1000)) * 1000;

    sv = select(sock + 1, NULL, &rfds, NULL, &timeout);
    if (sv < 0)
    {
        fprintf(stderr, "TCP connection broke\n");
        return -1;
    }
    if (sv == 0)
    {
        // no data available
        return 0;
    }
    rc = (int)recv(sock, (char*)s_io.bytes, (size_t)VPNX_MAX_PACKET_BYTES, 0);
    if (rc < 0)
    {
#ifdef Windows
        if (WSAGetLastError() == WSAEWOULDBLOCK)
        {
            return 0;
        }
        fprintf(stderr, "TCP read err=%d\n", WSAGetLastError());
#else
        if (errno == EWOULDBLOCK)
        {
            return 0;
        }
        fprintf(stderr, "TCP raad err=%d\n", errno);
#endif
        return rc;
    }
	s_io.type = VPNX_USBT_DATA;
	s_io.count = rc;
	*io = &s_io;
    return 0;
}

