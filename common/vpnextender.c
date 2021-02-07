
#include "vpnextender.h"
#include "vpnxtcp.h"
#if TUNNEL_BUILD
#include "tunnelsettings.h"
#endif

#define VPNX_DEFAULT_REMOTE_PORT    (2222)
#define VPNX_DEFAULT_REMOTE_HOST    "192.168.1.100"

/// USB device (opaque handle)
///
static void *s_usb_device = NULL;

/// state of connections on a single port
///
typedef struct tag_logi_conn
{
    /// TCP server socket
    ///
    SOCKET server_socket;

    /// THE TCP connection sockets
    ///
    SOCKET tcp_sockets[VPNX_MAX_CONNECTIONS];

    /// timer indicating we closed a socket, and waiting for the other side
    /// to ACK that by saying it closed its side. This is used to disable
    /// use of the connection slot for a new connection to avoid getting out 
    /// of sync
    ///
    #define VPNX_CLOSE_ACK_TIMEOUT (5) /* seconds */
    time_t tcp_closeacks[VPNX_MAX_CONNECTIONS];

    /// sequence number for close-acks. we expect the next ack to have
    /// the same sequence we sent the close msg for, and if we get
    /// one earlier we ignore it
    //
    #define VPNX_ACK_FLAG 0x8000
    #define VPNX_MAX_ACK 63
    uint16_t ackseq;

    /// remote host to connect to
    ///
    char remote_host[1024];

    /// remote port to connect to
    ///
    uint16_t remote_port;

    /// local port to listen on
    ///
    uint16_t local_port;
    
    /// last socket slot used, for round-robin
    ///
    int last_slot;
}
connection_t;

/// array of connection data
///
static connection_t s_cons[VPNX_MAX_PORTS];

/// last connection serviced, for round robin
///
static int s_last_conn;

/// mode: client or server
///
static int s_mode;

/// log level, 0 = errors only, up to 5 = verbose
///
static int s_log_level;

/// logging buffer, for windowing guis
///
static char s_log_buffer[1024];

/// logging output function
///
static void (*s_log_output_function)(const char *);

void vpnx_log(int level, const char *fmt, ...)
{
    va_list args;

    if (level > s_log_level)
    {
        return;
    }
    va_start(args, fmt);
    
    if (s_log_output_function)
    {
        vsnprintf(s_log_buffer, sizeof(s_log_buffer), fmt, args);
        s_log_output_function(s_log_buffer);
    }
    else 
    {
        if (level != 0)
        {
            vprintf(fmt, args);
        }
        else
        {
            vfprintf(stderr, fmt, args);
        }
    }
    va_end(args);
}

/// ring buffer for logging in to memory
///
#define LOG_RING_SIZE (8192)
static char *s_log = NULL;
static int s_loghead;
static int s_logtail;
static int s_logcount;
static int s_logsize;
    
void vpnx_mem_logger(const char *msg)
{
    if (! s_log) 
    {
        s_log = (char *)malloc(LOG_RING_SIZE);
        if (! s_log)
        {
            return;
        }
        s_logcount = 0;
        s_loghead = 0;
        s_logtail = 0;
        s_logsize = LOG_RING_SIZE;
    }
    while (msg && *msg)
    {
        s_log[s_loghead++] = *msg++;
        if (s_loghead >= s_logsize)
        {
            s_loghead = 0;
        }
        if (s_logcount < s_logsize)
        {
            s_logcount++;
        }
    }    
}

void vpnx_get_log_string(char *msg, int nmsg)
{
    int count = 0;
    
    if (!msg || !nmsg)
    {
        return;
    }
    msg[0] = '\0';
    
    while (msg && (count < (nmsg - 1)))
    {
        if (s_logcount <= 0)
        {
            break;
        }
        msg[count++] = s_log[s_logtail++];
        if (s_logtail >= s_logsize)
        {
            s_logtail = 0;
        }
        s_logcount--;
        if (msg[count - 1] == '\n')
        {
            break;
        }
    }
    msg[count] = '\0';
}

void vpnx_set_log_function(void (*logging_func)(const char *msg))
{
    s_log_output_function = logging_func;
}

void vpnx_set_log_level(uint32_t newlevel)
{
    if (newlevel > 5)
    {
        newlevel = 5;
    }
    s_log_level = newlevel;
}

int vpnx_get_log_level()
{
    return s_log_level;
}

static uint16_t vpnx_pack_connslot(int c, int slot)
{
    return ((uint16_t)c << 8 | (uint16_t)slot);
}

static int vpnx_extract_connslot(uint16_t connection, int *c, int *slot)
{
    if (!c || !slot)
    {
        return -1;
    }
    *c = (int)(connection >> 8);
    *slot = (int)(connection & 0xFF);
    
    if (*c < 0 || *c >= VPNX_MAX_PORTS)
    {
        vpnx_log(0, "Bad connection number %d\n", *c);
        return -1;
    }
    
    if (*slot < 0 || *slot >= VPNX_MAX_CONNECTIONS)
    {
        vpnx_log(0, "Bad slot number %d\n", *slot);
        return -1;
    }
    return 0;   
}

void vpnx_dump_packet(const char *because, vpnx_io_t *io, int level)
{
    int c;
    int slot;
    int i;
    int j;
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
    vpnx_extract_connslot(io->connection, &c, &slot);
    vpnx_log(level, "%s pkt %4d bytes, type=%s param=%u connection[%d][%d]\n",
            because, io ? io->count : 0, typestr, io->param, c, slot);

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

    for (i = j = 0; i < VPNX_HEADER_SIZE + (int)io->count; i++)
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

int vpnx_set_network(const char *apname, const char *password)
{
    vpnx_io_t io_packet;    
    int result;
    int len;
    int off;
    
    if (!s_usb_device) {
        vpnx_log(0, "Extender not attached for setting vid/pid\n");
        return -1;
    }
    memset(&io_packet, 0, sizeof(io_packet));
    io_packet.type = VPNX_USBT_CONFIG_NETWORK;
    
    len = snprintf((char*)io_packet.bytes, sizeof(io_packet.bytes) - 32, "%s", apname);
    off = len + 2; // keep 0 byte between name and pass
    len += snprintf((char*)io_packet.bytes + off, sizeof(io_packet.bytes) - len - 3, "%s", password);
    
    io_packet.count = len;
    io_packet.param = 0;
    io_packet.connection = off;

    vpnx_dump_packet("USB Tx net config", &io_packet, 3);
    result = usb_write(s_usb_device, &io_packet);
    if (result)
    {
        vpnx_log(0, "USB packet write error setting network config\n");
        return result;
    }
    return 0;
}

int vpnx_set_vidpid(uint16_t vid, uint16_t pid)
{
    vpnx_io_t io_packet;    
    int result;

    if (!s_usb_device) {
        vpnx_log(0, "Extender not attached for setting vid/pid\n");
        return -1;
    }
    memset(&io_packet, 0, sizeof(io_packet));
    io_packet.type = VPNX_USBT_CONFIG_VIDPID;
    io_packet.count = 0;
    io_packet.param = vid;
    io_packet.connection = pid;

    vpnx_dump_packet("USB Tx vid/pid", &io_packet, 3);
    result = usb_write(s_usb_device, &io_packet);
    if (result)
    {
        vpnx_log(0, "USB packet write error setting vid/pid\n");
        return result;
    }
    return 0;
}

void vpnx_reboot_extender()
{
    vpnx_io_t io_packet;    
    int result;

    if (!s_usb_device) {
        vpnx_log(0, "Extender not attached for rebooting\n");
        return;
    }
    memset(&io_packet, 0, sizeof(io_packet));
    io_packet.type = VPNX_USBT_REBOOT;
    io_packet.count = 0;
    io_packet.param = 0xFEED;
    io_packet.connection = 0xFACE;

    vpnx_dump_packet("USB reboot", &io_packet, 3);
    result = usb_write(s_usb_device, &io_packet);
    if (result)
    {
        vpnx_log(0, "USB packet write error rebooting\n");
    }
}

int vpnx_run_loop_slice()
{
    static vpnx_io_t   io_packet;

    vpnx_io_t   *io_from_usb;
    vpnx_io_t   *io_from_tcp;
    vpnx_io_t   *io_to_usb;
    vpnx_io_t   *io_to_tcp;
    int result;
    
    int c;
    int slot;
    
    // run loop, just transfer data between usb and tcp connection
    //
    io_from_usb = NULL;
    io_from_tcp = NULL;
    io_to_usb = NULL;
    io_to_tcp = NULL;

    /*
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
            vpnx_dump_packet("Flushing usb data on connect", io_from_usb, 2);
        }
    }
    while (io_from_usb);
    */
    
    if (s_mode == VPNX_SERVER)
    {
        int numactive;
        int j;
        
        for (c = 0, numactive = 0; c < VPNX_MAX_PORTS; c++)
        {
            for (slot = 0; slot < VPNX_MAX_CONNECTIONS; slot++)
            {
                if (s_cons[c].tcp_sockets[slot] != INVALID_SOCKET)
                {
                    numactive++;
                }
            }
        }
        // round-robin access of server sockets
        //
        for (j = 0, c = s_last_conn; j < VPNX_MAX_PORTS; j++)
        {
            for (slot = 0; slot < VPNX_MAX_CONNECTIONS; slot++)
            {
                if (s_cons[c].tcp_sockets[slot] == INVALID_SOCKET)
                {
                    if (s_cons[c].tcp_closeacks[slot] != 0)
                    {
                        time_t now;
                        
                        time(&now);
                        
                        if (now > s_cons[c].tcp_closeacks[slot])
                        {                    
                            // dont re-use connection until other side closes its 
                            vpnx_log(2, "Connection close not ACKed, timed out aging\n");
                            s_cons[c].tcp_closeacks[slot] = 0;
                            break;
                        }
                    }
                    else
                    {
                        // ready to connect on [c][slot]
                        break;
                    }
                }
            }
        }
        // next time, accept on next port range first, skipping any inactive servers
        //
        j = 0;
        do
        {
            s_last_conn++;
            if (s_last_conn >= VPNX_MAX_PORTS)
            {
                s_last_conn = 0;
            }
        }
        while ((s_cons[s_last_conn].server_socket == INVALID_SOCKET) && (++j < VPNX_MAX_PORTS));
        
        // if there is an open slot, allow a connection to happen
        //
        if (slot < VPNX_MAX_CONNECTIONS)
        {
            // adjust accept timeout based on if this is the first/only connection
            //
            result = tcp_accept_connection(s_cons[c].server_socket, &s_cons[c].tcp_sockets[slot], (numactive == 0) ? 1000 : 0);
            if (result)
            {
                return result;
            }
            if (s_cons[c].tcp_sockets[slot] != INVALID_SOCKET)
            {
                // got us a live one. tell the USB side to connect
                //
                io_packet.type = VPNX_USBT_CONNECT;
                io_packet.count = 0;
                io_packet.param = 1;
                io_packet.connection = vpnx_pack_connslot(c, slot);
                
                // if we have a remote_host/port configured, add to packet
                //
                if (s_cons[c].remote_host[0])
                {
                    io_packet.count = snprintf((char*)io_packet.bytes, VPNX_MAX_PACKET_BYTES, "%s:%u",
                            s_cons[c].remote_host, s_cons[c].remote_port);
                    if (io_packet.count < 0)
                    {
                        closesocket(s_cons[c].tcp_sockets[slot]);
                        s_cons[c].tcp_sockets[slot] = INVALID_SOCKET;
                        vpnx_log(0, "hostname too long for packet\n");
                        return -1;
                    }
                }
                vpnx_log(1, "Local TCP Connection [%d][%d]\n", c, slot);
                vpnx_dump_packet("USB Tx[connect]", &io_packet, 3);

                result = usb_write(s_usb_device, &io_packet);
                if (result)
                {
                    vpnx_log(0, "USB[connect] write failed\n");
                    return result;
                }
            }
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
            // extract connection/slot from packet. we map tcp connections 1:1 on both sides for sanity;
            //
            result = vpnx_extract_connslot(io_from_usb->connection, &c, &slot);
            if (result) 
            {
                vpnx_log(0, "Bad connection number, dropping connect request\n");
            }
            if (! result)
            {
                if (s_cons[c].tcp_sockets[slot] != INVALID_SOCKET)
                {
                    vpnx_log(0, "Connection[%d][%d] not available?\n", c, slot);
                    result = -1;
                }
            }
            if (! result)
            {
                s_cons[c].remote_port = VPNX_DEFAULT_REMOTE_PORT;
                strncpy(s_cons[c].remote_host, VPNX_DEFAULT_REMOTE_HOST, sizeof(s_cons[c].remote_host) - 1);
                s_cons[c].remote_host[sizeof(s_cons[c].remote_host) - 1] = '\0';

                if (io_from_usb->count > 0)
                {
                    char *pcolon;;
                    
                    // extract remote host spec from message if available
                    strncpy(s_cons[c].remote_host, (const char*)io_from_usb->bytes, sizeof(s_cons[c].remote_host) - 1);
                    if (io_from_usb->count < sizeof(s_cons[c].remote_host))
                    {
                        s_cons[c].remote_host[io_from_usb->count] = '\0';
                    }
                    else
                    {
                        s_cons[c].remote_host[sizeof(s_cons[c].remote_host) - 1] = '\0';
                    }
                    pcolon = strchr(s_cons[c].remote_host, ':');
                    if (pcolon) 
                    {
                        *pcolon++ = '\0';
                        s_cons[c].remote_port = strtoul(pcolon, NULL, 10);
                    }
                    vpnx_log(3, "USB host specified remote host: %s on %s port %u\n",
                             s_cons[c].remote_host, (pcolon) ? "specified" : "default", s_cons[c].remote_port);
                }
                vpnx_log(1, "USB host connects, Attempt to connect to TCP %s:%u\n", s_cons[c].remote_host, s_cons[c].remote_port);
                
                result = tcp_connect(s_cons[c].remote_host, s_cons[c].remote_port, &s_cons[c].tcp_sockets[slot]);
                if (s_cons[c].tcp_sockets[slot] == INVALID_SOCKET)
                {
                    // just in case, shouldn't ever happen if result non zero
                    result = -1;
                }
            }
            if (result)
            {
                io_to_usb = io_from_usb;

                vpnx_log(0, "Can't connect to remote host\n");
                s_cons[c].tcp_sockets[slot] = INVALID_SOCKET;
                
                // tell USB host the connect failed so it can retry if it wants
                //
                s_cons[c].ackseq++;
                if (s_cons[c].ackseq > VPNX_MAX_ACK)
                {
                    s_cons[c].ackseq = 0;
                }
                io_to_usb->type = VPNX_USBT_CLOSE;
                io_to_usb->param = s_cons[c].ackseq;
                io_to_usb->count = 0;
            }
            else
            {
                vpnx_log(2, "Connected [%d][%d]\n", c, slot);
                io_to_usb = NULL;
            }
            break;
        case VPNX_USBT_CLOSE:
            result = vpnx_extract_connslot(io_from_usb->connection, &c, &slot);
            if (result) 
            {
                vpnx_log(0, "Bad connection number, dropping close request\n");
                break;
            }
            vpnx_log(1, "USB host informs us their TCP connection[%d][%d] seq[%04X] is closed\n",
                    c, slot, io_from_usb->param);
                
            if (s_cons[c].tcp_sockets[slot] != INVALID_SOCKET)
            {
                if (io_from_usb->param & VPNX_ACK_FLAG)
                {                    
                    // this is an unexpeced ack, perhaps from a timed out connection, so ignore it
                    //
                    vpnx_log(3, "USB host acked close of open connection[%d][%d]? ignoring\n", c, slot);
                }
                else
                {
                    // they are closed first, so close ours too and ack with same ack as in packet (param)
                    //
                    vpnx_log(2, "Disconnected [%d][%d]\n", c, slot);
                    closesocket(s_cons[c].tcp_sockets[slot]);
                    s_cons[c].tcp_sockets[slot] = INVALID_SOCKET;
                    s_cons[c].tcp_closeacks[slot] = 0;
                    
                    // and tell the usb side we're closed too
                    io_to_usb = io_from_usb;
                    io_to_usb->param |= VPNX_ACK_FLAG;
                    io_to_usb->type = VPNX_USBT_CLOSE;
                    io_to_usb->count = 0;                    
                }
            }
            else
            {
                if (io_from_usb->param & VPNX_ACK_FLAG)
                {
                    uint16_t ack;
                    uint16_t expack;
                    
                    // ack from the usb host
                    ack = io_from_usb->param & ~VPNX_ACK_FLAG;
                    
                    // expected ack is the one we sent in clos message
                    expack = s_cons[c].ackseq;
                    
                    if (ack != expack)
                    {
                        vpnx_log(3, "USB host ACKs seq %d closed connection[%d][%d] out of seqeunce, expected %d\n",
                                       ack, c, slot, expack);
                    }
                    else
                    {
                        vpnx_log(3, "USB host ACKs closed connection[%d][%d], ok\n", c, slot);
                        s_cons[c].tcp_closeacks[slot] = 0;
                    }
                }
                else 
                {
                    vpnx_log(3, "USB host sent CLOS for already closed connection[%d], treating as ack\n", c, slot);
                    s_cons[c].tcp_closeacks[slot] = 0;
                }
            }
            break;
#if TUNNEL_BUILD
        case VPNX_USBT_CONFIG_NETWORK:
            {
                const char *netname;
                const char *netpass;
                
                netname = io_from_usb->bytes + io_from_usb->param;
                netpass = io_from_usb->bytes + io_from_usb->connection;
                
                result = tunnel_set_netconfig(netname, netpass);
                result = 0; // ignore failure?
            }
            break;
        case VPNX_USBT_CONFIG_VIDPID:
            {
                uint16_t vid;
                uint16_t pid;
                
                vid = io_from_usb->param;
                pid = io_from_usb->connection;

                result = tunnel_set_vidpid(vid, pid);
                result = 0; // ignore failure?
            }
            break;
        case VPNX_USBT_REBOOT:
            vpnx_log(1, "Rebooting\n");
            tunnel_reboot_device();
            break;
#endif
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
        result = vpnx_extract_connslot(io_from_usb->connection, &c, &slot);
        if (result) 
        {
            vpnx_log(0, "Bad connection number, dropping tcp send request\n");
        }
        else if (s_cons[c].tcp_sockets[slot] != INVALID_SOCKET)
        {
            vpnx_dump_packet("TCP Tx", io_to_tcp, 3);
            result = tcp_write(s_cons[c].tcp_sockets[slot], io_to_tcp);
            if (result != 0)
            {
                vpnx_log(0, "TCP write failed, closing connection[%d]\n", io_to_tcp->connection);
                // assume TCP connection is closed, so tell USB side
                closesocket(s_cons[c].tcp_sockets[slot]);
                s_cons[c].tcp_sockets[slot] = INVALID_SOCKET;
                // wait for usb side to close its tcp connection on same channel (for a bit)
                time(&s_cons[c].tcp_closeacks[slot]);
                s_cons[c].tcp_closeacks[slot] += VPNX_CLOSE_ACK_TIMEOUT;
                
                s_cons[c].ackseq++;
                if (s_cons[c].ackseq > VPNX_MAX_ACK)
                {
                    s_cons[c].ackseq = 0;
                }
                io_to_usb = &io_packet;
                io_to_usb->count = 0;
                io_to_usb->connection = vpnx_pack_connslot(c, slot);
                io_to_usb->param = s_cons[c].ackseq;
                io_to_usb->type = VPNX_USBT_CLOSE;
                result = 0;
            }
        }
        else
        {
            vpnx_log(0, "Dropping packet of %d bytes, no TCP connection\n", io_to_tcp->count);
        }
    }
    if (!io_to_usb)
    {
        static int last_conn_port = 0;
        
        // read tcp data packets, but only if we aren't already wanting to write usb data already
        //
        // serve slots round-robin style by connection inside port,
        // i.e. port[0].sock[0], port[1].sock[0], port[2].sock[0], ...
        //      port[0].sock[1], port[1].sock[1], ...
        //      port[0].sock[2], ...
        //
        c = last_conn_port;

        do
        {
            c++;
            if (c >= VPNX_MAX_PORTS) 
            {
                c = 0;
            }
            slot = s_cons[c].last_slot;
            
            if ((s_mode != VPNX_SERVER) || (s_cons[c].server_socket != INVALID_SOCKET))
            {                
                do
                {
                    slot++;
                    if (slot >= VPNX_MAX_CONNECTIONS)
                    {
                        slot = 0;
                    }
                    if (s_cons[c].tcp_sockets[slot] != INVALID_SOCKET)
                    {
                        break;
                    }
                }
                while (slot != s_cons[c].last_slot);
                
                s_cons[c].last_slot = slot;
                
            }
        }
        while (c != last_conn_port &&  s_cons[c].tcp_sockets[slot] == INVALID_SOCKET);
    
        last_conn_port = c;
                
        if (s_cons[c].tcp_sockets[slot] != INVALID_SOCKET)
        {
            vpnx_log(6, "now serving [%d][%d]\n", c, slot);
            
            result = tcp_read(s_cons[c].tcp_sockets[slot], &io_from_tcp);
            if (result)
            {
                vpnx_log(1, "TCP read failed, closing connection[%d][%d]\n", c, slot);
                // assume TCP connection is closed, so tell USB side
                closesocket(s_cons[c].tcp_sockets[slot]);
                s_cons[c].tcp_sockets[slot] = INVALID_SOCKET;
                // wait for usb side to close its tcp connection on same channel (for a bit)
                time(&s_cons[c].tcp_closeacks[slot]);
                s_cons[c].tcp_closeacks[slot] += VPNX_CLOSE_ACK_TIMEOUT;
                
                s_cons[c].ackseq++;
                if (s_cons[c].ackseq > VPNX_MAX_ACK)
                {
                    s_cons[c].ackseq = 0;
                }
                io_to_usb = &io_packet;
                io_to_usb->param = s_cons[c].ackseq;
                io_to_usb->connection = vpnx_pack_connslot(c, slot);
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
                    io_from_tcp->param = 1;
                    io_from_tcp->connection = vpnx_pack_connslot(c, slot);
                    io_to_usb = io_from_tcp;
                    io_to_usb->type = VPNX_USBT_DATA;
                    vpnx_dump_packet("TCP Rx", io_from_tcp, 3);
                }
            }
        }
    }
    if (io_to_usb)
    {
        result = vpnx_extract_connslot(io_to_usb->connection, &c, &slot);
        if (result)
        {
            vpnx_log(0, "Bad connection, dropping packet to usb\n");
            result = -1;
        }
        else
        {
            vpnx_dump_packet("USB Tx", io_to_usb, 3);
            result = usb_write(s_usb_device, io_to_usb);
        }
        if (result)
        {
            vpnx_log(0, "USB packet write error\n");
            return result;
        }
        io_to_usb = NULL;
    }
    return result;
}

int vpnx_run_loop_init(
                        int mode,
                        void* usb_device,
                        const char *remote_hosts[VPNX_MAX_PORTS],
                        uint16_t remote_ports[VPNX_MAX_PORTS],
                        uint16_t local_ports[VPNX_MAX_PORTS]
                    )
{
    int result;
    int numactive;
    int c;
    int i;
    
    static bool wasInited = false;
    
    if (! wasInited)
    {
        // first-time initialization
        //
#ifdef Windows
        // init win32 wsa
        WORD    wVersionRequested;
        WSADATA wsaData;
        
        wVersionRequested = MAKEWORD( 1, 1 );
    
        WSAStartup(wVersionRequested, &wsaData);
#endif                
        for (c = 0; c < VPNX_MAX_PORTS; c++)
        {
            for (i = 0; i < VPNX_MAX_CONNECTIONS; i++)
            {
                s_cons[c].server_socket     = INVALID_SOCKET;
                s_cons[c].tcp_sockets[i]    = INVALID_SOCKET;
                s_cons[c].tcp_closeacks[i]  = 0;
            }
        }
        s_last_conn = 0;
        wasInited = true;
    }
    s_mode = mode;
    s_usb_device = usb_device;

    for (c = 0, numactive = 0; c < VPNX_MAX_PORTS; c++)
    {
        // this is restartable, so make sure we're cleaned up
        //
        if (s_cons[c].server_socket != INVALID_SOCKET)
        {
            closesocket(s_cons[c].server_socket);
        }
        s_cons[c].server_socket = INVALID_SOCKET;
    
        for (i = 0; i < VPNX_MAX_CONNECTIONS; i++)
        {
            if (s_cons[c].tcp_sockets[i] != INVALID_SOCKET)
            {
                closesocket(s_cons[c].tcp_sockets[i]);
            }
            s_cons[c].tcp_sockets[i] = INVALID_SOCKET;
        }
        s_cons[c].last_slot = 0;

        if (remote_hosts[c] != NULL && local_ports[c] != 0 && remote_ports[c] != 0)
        {
            strncpy(s_cons[c].remote_host, remote_hosts[c], sizeof(s_cons[c].remote_host) - 1);
            s_cons[c].remote_host[sizeof(s_cons[c].remote_host) - 1] = '\0';
        
            s_cons[c].remote_port = remote_ports[c];
            s_cons[c].local_port  = local_ports[c];
            
            if (s_mode == VPNX_SERVER)
            {
                // if server-mode, listen for connections on our local port
                //
                result = tcp_listen_on_port(s_cons[c].local_port, &s_cons[c].server_socket);
                if (result)
                {
                    s_cons[c].server_socket = INVALID_SOCKET;
                    // allow some bad servers?
                    //return result;
                }
                else
                {
                    numactive++;
                }
            }
        }
    }
    return (numactive > 0) ? 0 : -1;
}

