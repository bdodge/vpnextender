
#include "vpnxtcp.h"
#include "vpnextender.h"

int tcp_listen_on_port(uint16_t port, SOCKET *socket_ptr)
{
    struct sockaddr_in serv_addr;
    #ifdef Windows
    unsigned long nonblock;
    #else
    uint32_t nonblock;
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
    nonblock = 0;
    result = ioctlsocket(sock, FIONBIO, &nonblock);
    if (result < 0)
    {
        return result;
    }
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
    result = listen(sock, 2);
    if (result < 0)
    {
        closesocket(sock);
        fprintf(stderr, "Can't listen on port\n");
    }
    printf("Listening on port %u for TCP\n", port);
    *socket_ptr = sock;
    return result;
}

int tcp_accept_connection(SOCKET serversock, SOCKET *clientsock_ptr)
{
    struct sockaddr_in cli_addr;
    socklen_t clilen;
    SOCKET clientsock;

    printf("Accepting connections now\n");
    
    clilen = sizeof(cli_addr);
    clientsock = accept(serversock, (struct sockaddr *)&cli_addr, &clilen);

    printf("Connection: %d\n", clientsock);
    
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

int tcp_connect(const char *host, uint16_t port, SOCKET *socket_ptr)
{
    struct sockaddr_in serv_addr;
    SOCKET sock;
    #ifdef Windows
    unsigned long nonblock;
    #else
    uint32_t nonblock;
    #endif
    int result;
    bool isname;
    int i;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        fprintf(stderr, "Can't create server socket\n");
        return -1;
    }
	#if 1
    nonblock = 1;
    if (ioctlsocket(sock, FIONBIO, &nonblock) < 0)
    {
        fprintf(stderr, "Can't make nonblocking");
        closesocket(sock);
        return -1;
    }
	#endif
	
    // if host is an IP address, use directly
	//
    for (i = 0, isname = false; i < strlen(host); i++)
    {
        if ((host[i] < '0' || host[i] > '9') && host[i] != '.')
        {
            isname = true;
            break;
        }
    }
    if (isname)
    {
        struct hostent *hostname = gethostbyname(host);

        if (! hostname)
        {
            fprintf(stderr, "Can't find address %s\n", host);
            closesocket(sock);
            return -1;
        }
        memcpy(&serv_addr.sin_addr, hostname->h_addr, hostname->h_length);
    }
    else
    {
        if (! inet_aton(host, &serv_addr.sin_addr))
        {
            fprintf(stderr, "Invalid address %s\n", host);
            closesocket(sock);
            return -1;
        }
    }
    // connect to remote server
    //
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    result = connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (result < 0)
    {
        // this is non blocking, so expect error.
        //
        #ifdef windows
        if (WSAGetLastError() == WSAEWOULDBLOCK)
        #else
        if (errno == EWOULDBLOCK || errno == EINPROGRESS)
        #endif
        {
            result = 0;
        }
        else
        {
            fprintf(stderr, "Can't connect to remote host\n");
            closesocket(sock);
            return -1;
        }
    }
	*socket_ptr = sock;
	return 0;
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
	
    // short wait for tcp read, to keep checking usb
	//
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
    if (rc <= 0)
    {
#ifdef Windows
		if (WSAGetLastError() == WSAEWOULDBLOCK)
		{
			return 0;
		}
#else
		if (errno == EWOULDBLOCK || errno == EAGAIN)
		{
			return 0;
		}
#endif
		// 0 count after select > 0 means socket closed
        return rc;
    }
	s_io.type = VPNX_USBT_DATA;
	s_io.count = rc;
	*io = &s_io;
    return 0;
}

void vpnx_dump_packet(const char *because, vpnx_io_t *io, int level)
{
    int i, j;
    char pkt_text[18];
    uint8_t data;

    printf("Pkt %4d bytes  %s\n", io ? io->count : 0, because);

    if (!io)
    {
        printf("<nil>\n");
        return;
    }
    printf("Count=%u bytes\n", io->count);
    printf("Type=%u\n", io->type);

    if (!io->count)
    {
        printf("  <empty>\n");
        return;
    }
    level++;

    for (i = j = 0; i < io->count; i++)
    {
        data = io->bytes[i];

        printf("%02X ", data);
        if (data >= ' ' && data <= '~')
        {
            pkt_text[j] = data;
        }
        else
        {
            pkt_text[j] = '.';
        }
        pkt_text[++j] = '\0';

        if (j == 16)
        {
            printf( "    | %s |\n", pkt_text);
            j = 0;
        }
    }
    if (j > 0)
    {
        while (j < 16)
        {
            printf("   ");
            pkt_text[j++] = '-';
        }
        pkt_text[j] = '\0';
        printf("    | %s |\n", pkt_text);
    }
    printf("\n");
}

