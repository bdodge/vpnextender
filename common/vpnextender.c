
#include "vpnextender.h"
#include "vpnxtcp.h"

/// USB device (opaque handle)
///
static void *s_usb_device;

/// TCP server socket
///
static SOCKET s_server_socket;

/// THE TCP connection socket
///
static SOCKET s_tcp_socket;

/// remote host to connect to
///
char s_remote_host[1024];

/// remote port to connect to
///
uint16_t s_remote_port;

/// local port to listen on
///
uint16_t s_local_port;

/// mode: client or server
///
static int s_mode;

/// log level, 0 = errors only, up to 5 = verbose
///
static int s_log_level;

void vpnx_log(int level, const char *fmt, ...)
{
    va_list args;

    if (level > s_log_level)
    {
        return;
    }
    va_start(args, fmt);
    if (level != 0)
    {
        vprintf(fmt, args);
    }
    else
    {
        vfprintf(stderr, fmt, args);
    }
    va_end(args);
}

void vpnx_set_log_level(uint32_t newlevel)
{
    if (newlevel > 5)
    {
        newlevel = 5;
    }
    s_log_level = newlevel;
}

void vpnx_dump_packet(const char *because, vpnx_io_t *io, int level)
{
    int i, j;
    char pkt_text[18];
    const char *typestr;
    uint8_t data;

    if (!io)
    {
        return;
    }
    if (!because)
    {
        because = "";
    }
    switch (io->type)
    {
    case VPNX_USBT_MSG:
        typestr = "MESG";
        break;
    case VPNX_USBT_DATA:
        typestr = "DATA";
        break;
    case VPNX_USBT_PING:
        typestr = "PING";
        break;
    case VPNX_USBT_SYNC:
        typestr = "SYNC";
        break;
    case VPNX_USBT_CONNECT:
        typestr = "CONN";
        break;
    case VPNX_USBT_CLOSE:
        typestr = "CLOS";
        break;
    default:
        vpnx_log(1, "dump-packet: bad type %d\n", io->type);
        return;
        break;
    }
    vpnx_log(level, "%s pkt %4d bytes, type=%s\n", because, io ? io->count : 0, typestr);

    if (!io->count)
    {
        vpnx_log(5, "    <empty>\n");
        return;
    }
    if (io->count > VPNX_MAX_PACKET_BYTES)
    {
        vpnx_log(1, "dump-packet: bad count %d\n", io->count);
        return;
    }
        
    level++;

    for (i = j = 0; i < (int)io->count; i++)
    {
        data = ((uint8_t*)io)[i]; //io->bytes[i];

        vpnx_log(level, "%02X ", data);
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
            vpnx_log(level,  "    | %s |\n", pkt_text);
            j = 0;
        }
    }
    if (j > 0)
    {
        while (j < 16)
        {
            vpnx_log(level, "   ");
            pkt_text[j++] = '-';
        }
        pkt_text[j] = '\0';
        vpnx_log(level, "    | %s |\n", pkt_text);
    }
    vpnx_log(level, "\n");
}

int vpnx_run_loop_slice()
{
    static vpnx_io_t   io_packet;
    
    char remote_host[1024];
    uint16_t remote_port;
    
    vpnx_io_t   *io_from_usb;
    vpnx_io_t   *io_from_tcp;
    vpnx_io_t   *io_to_usb;
    vpnx_io_t   *io_to_tcp;
    int result;

    // run loop, just transfer data between usb and tcp connection
    //
    io_from_usb = NULL;
    io_from_tcp = NULL;
    io_to_usb = NULL;
    io_to_tcp = NULL;

    if (s_mode == VPNX_SERVER && s_tcp_socket == INVALID_SOCKET)
    {
        // wait for a connection, nothing to do till then
        //
        result = tcp_accept_connection(s_server_socket, &s_tcp_socket);
        if (result)
        {
            return result;
        }
        // got us a live one. tell the USB side to connect
        //
        io_packet.type = VPNX_USBT_CONNECT;
        io_packet.count = 0;
        io_packet.srcport = 0xDEAD;
        io_packet.dstport = 0xBEEF;
        
        // if we have a remote_host/port configured, add to packet
        //
        if (s_remote_host[0])
        {
            io_packet.count = snprintf((char*)io_packet.bytes, VPNX_MAX_PACKET_BYTES, "%s:%u",
                    s_remote_host, s_remote_port);
            if (io_packet.count < 0)
            {
                vpnx_log(0, "hostname too long for packet\n");
                return -1;
            }
        }
        vpnx_dump_packet("USB Tx[connect]", &io_packet, 3);

        // flush any old data in driver
        //
        do
        {
            io_from_usb = NULL;
            result = usb_read(s_usb_device, &io_from_usb);
            if (result)
            {
                vpnx_log(0, "USB packet read error\n");
                return result;
            }
            if (io_from_usb)
            {
                vpnx_dump_packet("Flushing usb data on connect", io_from_usb, 1);
            }
        }
        while (io_from_usb);

        result = usb_write(s_usb_device, &io_packet);
        if (result)
        {
            vpnx_log(0, "USB[connect] write failed\n");
            return result;
        }
    }    
    // read usb data/control packets
    //
    result = usb_read(s_usb_device, &io_from_usb);
    if (result)
    {
        vpnx_log(0, "USB packet read error\n");
        return result;
    }
    if (io_from_usb)
    {
        vpnx_dump_packet("USB Rx", io_from_usb, 3);

        switch (io_from_usb->type)
        {
        case VPNX_USBT_MSG:
            break;
        case VPNX_USBT_DATA:
            if (io_from_usb->count > VPNX_MAX_PACKET_BYTES)
            {
                vpnx_log(1, "Dropping data packet with bad length %d\n", io_from_usb->count);
                io_from_usb = NULL;
            }
            io_to_tcp = io_from_usb;
            break;
        case VPNX_USBT_PING:
            break;
        case VPNX_USBT_SYNC:
            break;
        case VPNX_USBT_CONNECT:
            remote_port = s_remote_port;
            strncpy(remote_host, s_remote_host, sizeof(remote_host) - 1);
            remote_host[sizeof(remote_host) - 1] = '\0';
            if (io_from_usb->count > 0)
            {
                char *pcolon;;
                
                // extract remote host spec from message if available
                strncpy(remote_host, (const char*)io_from_usb->bytes, sizeof(remote_host) - 1);
                if (io_from_usb->count < sizeof(remote_host))
                {
                    remote_host[io_from_usb->count] = '\0';
                }
                else
                {
                    remote_host[sizeof(remote_host) - 1] = '\0';
                }
                pcolon = strchr(remote_host, ':');
                if (pcolon) 
                {
                    *pcolon++ = '\0';
                    remote_port = strtoul(pcolon, NULL, 10);
                }
                vpnx_log(3, "USB host specified remote host: %s on %s port %u\n",
                         remote_host, (pcolon) ? "specified" : "default", remote_port);
            }
            vpnx_log(1, "USB host connects, Attempt to connect to TCP %s:%u\n", remote_host, remote_port);
            result = tcp_connect(remote_host, remote_port, &s_tcp_socket);
            if (result)
            {
                io_to_usb = io_from_usb;

                vpnx_log(0, "Can't connect to remote host\n");
                s_tcp_socket = INVALID_SOCKET;
                
                // tell USB host the connect failed so it can retry if it wants
                //
                io_to_usb->type = VPNX_USBT_CLOSE;
                io_to_usb->count = 0;
            }
            else
            {
                vpnx_log(2, "Connected\n");
                io_to_usb = NULL;
            }
            break;
        case VPNX_USBT_CLOSE:
            vpnx_log(1, "USB host informs us their TCP connection is closed\n");
            if (s_tcp_socket != INVALID_SOCKET)
            {
                // close ours then too
                closesocket(s_tcp_socket);
                s_tcp_socket = INVALID_SOCKET;
            }
            break;
        default:
            vpnx_log(0, "Unimplemented USB packet type: %d\n", io_from_usb->type);
            io_from_usb = NULL;
            break;
        }
    }
    // if io_to_tcp, write that
    //
    if (io_to_tcp && io_to_tcp->count)
    {
        if (s_tcp_socket != INVALID_SOCKET)
        {
            vpnx_dump_packet("TCP Tx", io_to_tcp, 3);
            result = tcp_write(s_tcp_socket, io_to_tcp);
            if (result != 0)
            {
                vpnx_log(0, "TCP write failed, closing connection\n");
                // assume TCP connection is closed, so tell USB side
                closesocket(s_tcp_socket);
                s_tcp_socket = INVALID_SOCKET;
                io_to_usb = &io_packet;
                io_to_usb->count = 0;
                io_to_usb->type = VPNX_USBT_CLOSE;
                result = 0;
            }
        }
        else
        {
            vpnx_log(2, "Dropping packet of %d bytes, no TCP connection\n", io_to_tcp->count);
        }
    }
    if (!io_to_usb && (s_tcp_socket != INVALID_SOCKET))
    {
        // read tcp data packets, but only if we aren't already wanting to write usb data already
        //
        result = tcp_read(s_tcp_socket, &io_from_tcp);
        if (result)
        {
            vpnx_log(0, "TCP read failed, closing connection\n");
            // assume TCP connection is closed, so tell USB side
            closesocket(s_tcp_socket);
            s_tcp_socket = INVALID_SOCKET;
            io_to_usb = &io_packet;
            io_to_usb->count = 0;
            io_to_usb->type = VPNX_USBT_CLOSE;
            result = 0;
        }
        else
        {
            // if any data, write it to USB port
            //
            if (io_from_tcp && io_from_tcp->count)
            {
                vpnx_dump_packet("TCP Rx", io_from_tcp, 3);
                io_to_usb = io_from_tcp;
                io_to_usb->type = VPNX_USBT_DATA;
            }
        }
    }
    if (io_to_usb)
    {
        vpnx_dump_packet("USB Tx", io_to_usb, 3);
        result = usb_write(s_usb_device, io_to_usb);
        if (result)
        {
            vpnx_log(0, "USB packet write error\n");
            return result;
        }
    }
    return result;
}

int vpnx_run_loop_init(int mode, void* usb_device, const char *remote_host, uint16_t remote_port, uint16_t local_port)
{
    int result;
    
#ifdef Windows
    {
        // init win32 wsa
        WORD    wVersionRequested;
        WSADATA wsaData;
        
        wVersionRequested = MAKEWORD( 1, 1 );
    
        WSAStartup(wVersionRequested, &wsaData);
    }
#endif
    s_mode = mode;
    s_usb_device = usb_device;
    s_server_socket = INVALID_SOCKET;
    s_tcp_socket = INVALID_SOCKET;

    strncpy(s_remote_host, remote_host, sizeof(s_remote_host) - 1);
    s_remote_host[sizeof(s_remote_host) - 1] = '\0';

    s_remote_port = remote_port;
    s_local_port  = local_port;
    
    if (s_mode == VPNX_SERVER)
    {
        // if server-mode, listen for connections on our local port
        //
        result = tcp_listen_on_port(s_local_port, &s_server_socket);
        if (result)
        {
            return result;
        }
    }
    return 0;
}

