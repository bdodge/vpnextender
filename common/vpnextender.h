#ifndef VPNEXTENDER_H_
#define VPNEXTENDER_H_ 1

#if defined(Windows)
#include <winsock2.h>
#include <Ws2tcpip.h>
#include <Windows.h>
#include <Setupapi.h>
#include <shlwapi.h>
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>

#define VPNX_CLIENT (0) ///< connect to remote host/port from VPN from LAN via USB
#define VPNX_SERVER (1) ///< accept local connections and forward via USB to LAN

// default VID/PID of Printer device to look for
//
#define kVendorID		0x3f0
#define kProductID		0x102

/// how many data bytes can be sent via the usb transport in one packet
/// use a large enough size to transport typical network traffic but alway
/// make this NOT something that can be a typical usb packet size for
/// transfer, as we want the usb drivers in the os to always flush our
/// packets, so make it larger than 1024.  At about 65536, there's no
/// improvement in throughput.  At above 16384, there is some noticable
/// delay in "typing" single bytes.  It would be nice to make this dynamic
/// but that messes usb up a bit
///
/// the data payload is adjusted down to account for structure header
/// so the whole thing is some multiple of usb packet size
///
/// ALL usb transfers are in units of this one packet size so even
/// payloads of 1 byte use the entire packet size, keep that in mind
///
#define VPNX_HEADER_SIZE    (2 * sizeof(uint16_t) + 2 * sizeof(uint32_t))
#define VPNX_PACKET_SIZE    (16384)
#define VPNX_MAX_PACKET_BYTES (VPNX_PACKET_SIZE - VPNX_HEADER_SIZE)

/// types of packets
///
#define VPNX_USBT_DATA          0
#define VPNX_USBT_PING          1
#define VPNX_USBT_SYNC          2
#define VPNX_USBT_MSG           3
#define VPNX_USBT_CLOSE         4
#define VPNX_USBT_CONNECT       5

/// The USB data transfer packet type
///
typedef struct /*__attribute__((packed))*/ tag_vpnx_io
{
    uint32_t    type;
    uint32_t    count;
    uint16_t    srcport;
    uint16_t    dstport;
    uint8_t     bytes[VPNX_MAX_PACKET_BYTES];
}
vpnx_io_t;

/// make some things OS agnostic
///
#ifndef Windows
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket close
#define ioctlsocket ioctl
#else
typedef int socklen_t;
#endif

#define USB_CLASS_PRINTER       7
#define USB_CLASS_USER          0xFF

#ifdef Windows
    static const GUID s_guid_printer =
        { 0x28d78fad, 0x5a12, 0x11D1, { 0xae, 0x5b, 0x00, 0x00, 0xf8, 0x03, 0xa8, 0xc2 }};

    #define MALLOC malloc
    #define FREE   free
#elif defined(Linux)
    #define MALLOC malloc
    #define FREE   free
    
    #define MAX_PATH                512
    #define O_BINARY                0
#else
    #define MALLOC malloc
    #define FREE   free
#endif

/// usb read/write functions are supplied by the application as they
/// are much different on the host based proxy program vs the
/// the tunnel on Linux using a kernel driver
///
extern int usb_write(void *dev, vpnx_io_t *io);
extern int usb_read(void *dev, vpnx_io_t **io);

void vpnx_log(int level, const char *fmt, ...);
void vpnx_set_log_level(uint32_t newlevel);
void vpnx_dump_packet(const char *because, vpnx_io_t *io, int level);
int vpnx_run_loop_slice(void);
int vpnx_run_loop_init(int mode, void* usb_device, const char *remote_host, uint16_t remote_port, uint16_t local_port);

#endif
