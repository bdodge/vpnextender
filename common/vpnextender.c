
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

char s_remote_host[1024];
uint16_t s_remote_port;

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

int vpnx_run_loop_slice()
{
	vpnx_io_t	io_packet;
	
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
        
        result = usb_write(s_usb_device, &io_packet);
        if (result)
        {
            fprintf(stderr, "USB[connect] write failed\n");
            return result;
        }
	}
    // read usb data/control packets
    //
    result = usb_read(s_usb_device, &io_from_usb);
    if (result)
    {
        fprintf(stderr, "USB packet read error\n");
        return result;
    }
    if (io_from_usb)
    {
        printf("USB read %d type:%d\n", io_from_usb->count, io_from_usb->type);
		vpnx_dump_packet("USB Rx", io_from_usb, 0);

        switch (io_from_usb->type)
		{
		case VPNX_USBT_MSG:
			break;
		case VPNX_USBT_DATA:
			io_to_tcp = io_from_usb;
			break;
		case VPNX_USBT_PING:
			break;
		case VPNX_USBT_SYNC:
			break;
		case VPNX_USBT_CONNECT:
			printf("USB host connects, Attempt to connect to TCP %s:%u\n", s_remote_host, s_remote_port);
			result = tcp_connect(s_remote_host, s_remote_port, &s_tcp_socket);
			if (result)
			{
				fprintf(stderr, "Can't connect to remote host\n");
				s_tcp_socket = INVALID_SOCKET;
				
				// tell USB host the connect failed so it can retry if it wants
				//
				io_to_usb->type = VPNX_USBT_CLOSE;
				io_to_usb->count = 0;
			}
			else
			{
				printf("Connected!\n");
				io_to_usb = NULL;
			}
			break;
		case VPNX_USBT_CLOSE:
			printf("USB host informs us their TCP connction is closed\n");
			if (s_tcp_socket != INVALID_SOCKET)
			{
				// close ours then too
				closesocket(s_tcp_socket);
				s_tcp_socket = INVALID_SOCKET;
			}
			break;
		default:
			fprintf(stderr, "Unimplemented USB packet typ: %d\n", io_from_usb->type);
			break;
		}
	}
	// if io_to_tcp, write that
	//
	if (io_to_tcp && io_to_tcp->count)
	{
		printf("TCP write %d\n", io_to_tcp->count);
		if (s_tcp_socket != INVALID_SOCKET)
		{
			result = tcp_write(s_tcp_socket, io_to_tcp);
			if (result != 0)
			{
				fprintf(stderr, "TCP write failed\n");
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
			printf("Dropping packet of %d bytes, no TCP connection\n", io_to_tcp->count);
		}
	}
	if (!io_to_usb && (s_tcp_socket != INVALID_SOCKET))
	{
		// read tcp data packets, but only of we aren't already wanting to write usb data
		//
		result = tcp_read(s_tcp_socket, &io_from_tcp);
		if (result)
		{
			fprintf(stderr, "TCP read error\n");
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
				io_to_usb = io_from_tcp;
				io_to_usb->type = VPNX_USBT_DATA;
			}
		}
	}
	if (io_to_usb)
	{
		printf("USB write %d type:%d\n", io_to_usb->count, io_to_usb->type);
		vpnx_dump_packet("USB Tx", io_to_usb, 0);
		result = usb_write(s_usb_device, io_to_usb);
		if (result)
		{
			fprintf(stderr, "USB packet write error\n");
			return result;
		}
	}
	return result;
}

int vpnx_run_loop_init(void* usb_device, const char *remote_host, uint16_t remote_port)
{
	s_usb_device = usb_device;
	s_server_socket = INVALID_SOCKET;
	s_tcp_socket = INVALID_SOCKET;
	strncpy(s_remote_host, remote_host, sizeof(s_remote_host) - 1);
	s_remote_host[sizeof(s_remote_host) - 1] = '\0';
	s_remote_port = remote_port;
    return 0;
}

