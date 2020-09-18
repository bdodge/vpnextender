//
// TCP/IP <---> USB device tunnel. 
//
// Server Mode:
// This program listens for TCP connections on a specific port (typically SSH) and
// when a connection is made, marshalls TCP and USB data between the network connected host
// and USB connected host.
//
// Client Mode:
// This program listens for USB data from a host and when instructed, connects via
// TCP to a host/port on the LAN, and then marshalls data between the USB and TCP
// connections transparently.
//
// In both modes, there needs to be the USB proxy program running on the
// USB connected host which is running in the opposite mode as this program
//
// If the proxy program is running in server mode, the tunnel (this program) runs in client
// mode. In this scenario, some program running in the proxy host (typically a VPN connected
// PC) that wants to connect to a LAN hst, will connect to "localhost:localport" where localport
// is the listening port of the proxy program. That connection will cause a USB packet to be sent
// here with a "connect" type, which will prompt this program to connect to the LAN host
// (specified on the command line, or via the connect packet).
//
// If this proxy program is running in client mode, the tunnel runs in server mode. In
// this scenario, some program running on some machine in the LAN that wishes to connect
// to a server in the VPN will connect via TCP to this host's IP:port, where port is the
// localport specified on the command line. That connection will prompt us to send a USB
// packet "connect" to the proxy program which will prompt it to connect to the remote host
// on the VPN
//
// In both scenarios, once a connection is established via TCP, data is just transported
// transparently between the TCP and USB connections in packets
//
// The data is wrapped in fixed blocks corresponding to the USB endpoint packet size
// which is expected to be (at least) a USB high-speed enumeration with a packet
// size of 512 bytes, but full speed connections should work as well but much slower.
//
#include "vpnextender.h"
#include "vpnxtcp.h"

/// The TCP port to marshall data on (ssh)
///
static int s_tunnel_port = 22;

/// The USB device connection
///
static int s_usbd = -1;

static int s_connected, s_rxcnt, s_txcnt;

#if 1

static void SetLED(int led, int onoff)
{
	const char *ldev, *tdev;
	int lfd[4] = { -1, -1, -1, -1 };
	int tfd[4] = { -1, -1, -1, -1 };
	unsigned char val[8];
	
	switch (led)
	{
	case 2:
		ldev = "/sys/devices/leds.7/leds/beaglebone:green:usr2/brightness";
		tdev = "/sys/devices/leds.7/leds/beaglebone:green:usr2/brightness";
		break;
	case 3:
		ldev = "/sys/devices/leds.7/leds/beaglebone:green:usr3/brightness";
		tdev = "/sys/devices/leds.7/leds/beaglebone:green:usr2/brightness";
		break;
	default:
		return;
	}
	if (onoff < 0)
	{
		if (lfd[led] > 0)
		{
			close(lfd[led]);
			lfd[led] = -1;
		}
		return;
	}
	val[0] = onoff ? '1' : '0';
	val[1] = '\n';
	if (tfd[led] < 0)
	{
		tfd[led] = open(tdev, O_RDWR, 0660);
		if (tfd[led] > 0)
		{
			write(tfd[led], "none", 4);
			close(tfd[led]);
		}
	}
	if (lfd[led] < 0)
	{
		lfd[led] = open(ldev, O_RDWR, 0660);
	}
	if (lfd[led] > 0)
	{
		int wc = write(lfd[led], val, 1);
		if (wc < 0)
			vpnx_log(5, "err=%d\n", errno);
		//close(lfd[led]);
		//lfd[led] = -1;
	}
}

#else

static void SetLED(int led, int onoff)
{
	return;
}

#endif
	
int usb_open_device(long vid, long pid)
{
    static int s_probed = 0;

	char    driverName[128];
    char    sysCmd[100];

    if (! s_probed)
    {
        s_probed = 1;

	    // system("rmmod -f g_multi");
	    snprintf(sysCmd, sizeof(sysCmd),
	            "modprobe g_printer"
	            " idVendor=%ld idProduct=%ld qlen=16",
	            vid, pid
	         );
	    system(sysCmd); 
    }
    // open the appropriate device for the endpoint
    //
    snprintf(driverName, sizeof(driverName), "/dev/g_printer0");
    if ((s_usbd = open(driverName, O_RDWR, 0666)) < 0)
    {
        vpnx_log(0, "Can't open driver %s\n", driverName);
        return -1;
    }
    else
    {
        unsigned long   iparm;

		// setup nonblocking IO on driver
		//
        iparm = 1;
        ioctl(s_usbd, FIONBIO, (unsigned long*)&iparm);
    }
    vpnx_log(1, "USB Device %s opened\n", driverName);
   	return 0;
}

int usb_write(void *pdev, vpnx_io_t *io)
{
	int usbd = (int)(uintptr_t)pdev;
	uint8_t *psend;
	int tosend;
    int sent;
    int wc, sv;
    fd_set  wfds;
    struct  timeval timeout;
    
    int waitms = 15000;
    
	psend = (uint8_t *)io;
	tosend = VPNX_PACKET_SIZE; //io->count + VPNX_HEADER_SIZE;
    sent = 0;
	
	while (sent < tosend)
    {
        FD_ZERO (&wfds);
        FD_SET  (usbd, &wfds);
    
        timeout.tv_sec  = waitms / 1000;
        timeout.tv_usec = (waitms - ((waitms / 1000) * 1000)) * 1000;
    
        sv = select(usbd + 1, NULL, &wfds, NULL, &timeout);
        if (sv < 0)
        {
            vpnx_log(0, "USB connection broke\n");
            return -1;
        }
        if (sv == 0)
        {
            vpnx_log(0, "USB connection blocked\n");
            return -1;
        }
        wc = (int)write(usbd, (char*)psend + sent, (size_t)(tosend - sent));
        if (wc < 0)
        {
#ifdef Windows
            vpnx_log(0, "USB write err=%d\n", WSAGetLastError());
#else
            vpnx_log(0, "USB write err=%d\n", errno);
#endif
            return wc;
        }
        sent += wc;
		vpnx_log(5, "usbchunk=%d, %d to go\n", wc, tosend - sent);
    }
    return 0;
}

static int usb_read_bytes(int fd, uint8_t *data, int count, int waitms, bool block)
{
    int rc;
	int sv;
	int gotten;
    fd_set  rfds;
    struct  timeval timeout;
    
    FD_ZERO (&rfds);
    FD_SET  (fd, &rfds);
	
	gotten = 0;
	while (gotten < count)
	{
	    timeout.tv_sec  = waitms / 1000;
	    timeout.tv_usec = (waitms - ((waitms / 1000) * 1000)) * 1000;

	    sv = select(fd + 1, NULL, &rfds, NULL, &timeout);
	    if (sv < 0)
	    {
	        vpnx_log(0, "USB connection broke\n");
	        return -1;
	    }
	    if (sv == 0)
	    {
	        // no data available
			if (gotten)
			{
				vpnx_log(0, "USB Timeout reading %d bytes\n", count);
				return -1;
			}
	        return 0;
	    }
	    rc = (int)read(fd, (char*)data + gotten, (count - gotten));
	    if (rc <= 0)
	    {
			if (errno == EAGAIN)
			{
				// confused USB driver doesnt know that select > 0, read == 0 is bad thing
				//
				if (block)
				{
					continue;
				}
				else
				{
					return 0;
				}
			}
			// read of 0 after select of 1 means closed
			//
			return rc;
		}
		if (rc)
		{
			// got SOME data, so make sure we get complete packet
			//
			block = true;
			waitms = 15000;
			gotten += rc;
			vpnx_log(5, "USB got %d more, %d read tot, %d remaining to get\n", rc, gotten, count - gotten);
		}
	}
    return count;
}

int usb_read(void *pdev, vpnx_io_t **io)
{
	int usbd = (int)(uintptr_t)pdev;
	static vpnx_io_t s_io;
    int rc;
    
	*io = NULL;

	//vpnx_log(3, "usb read try for %d\n", VPNX_PACKET_SIZE);
	
	// short wait for read, to keep checking tcp (note: timeouts of 10ms or less dont seem to work)
	//
    rc = usb_read_bytes(usbd, (uint8_t*)&s_io, VPNX_PACKET_SIZE, 100, false);
	//vpnx_log(3, "Got %d\n", rc);
	if (rc <= 0)
	{
		return rc;
	}
	if (rc < VPNX_HEADER_SIZE)
	{
		vpnx_log(5, "usb read: short read of only %d\n", rc);
		return 0;
	}
	*io = &s_io;
    return 0;
}

void SignalHandler(int sigraised)
{
	vpnx_log(0, "\nInterrupted.\n");
	
    exit(0);
}

static int useage(const char *progname)
{
    vpnx_log(0, "Usage: %s -c [-h][-v VID][-p PID][-l loglevel][-r remote-port] [remote-host]\n", progname);
	vpnx_log(0, "   or: %s -s -a local-port [-h][-v VID][-p PID][-l loglevel]\n\n", progname);
    vpnx_log(0, "  -c VPN->LAN client\n");
	vpnx_log(0, "       A USB connection is translated to a TCP connection to the LAN based remote host\n");
    vpnx_log(0, "       specified in the USB connection, or if not present, the default remote host specified\n");
	vpnx_log(0, "       on the command line\n");
    vpnx_log(0, "    -r default port on remote host to access\n");
    vpnx_log(0, "    remote-host, if supplied, is the default remote host to connect to when a USB\n");
	vpnx_log(0, "	 connection is initiated by the prt proxy and the request doesn't contain a host name\n");
	vpnx_log(0, "\n");
    vpnx_log(0, "  -s LAN->VPN server\n");
    vpnx_log(0, "       Listen on local-port for TCP connections and translate them to\n");
    vpnx_log(0, "       USB connections to the host prt proxy on a VPN connected PC\n");
    vpnx_log(0, "    -a (or -r) local port to listen on for incoming TCP connections\n");
    vpnx_log(0, "    -v use this vendor id (VID) for the USB device\n");
    vpnx_log(0, "    -p use this product id (PID) for the USB device\n");
    vpnx_log(0, "    -h This help text\n");
    return -1;
}

int main(int argc, const char *argv[])
{
	int         mode;
    char        remote_host[256];
    uint16_t    port;
    bool        secure;
    int         loglevel;
    const char *progname;
    int         argdex;
    const char *arg;
    int         result;
    
    long	    usbVendor = kVendorID;
    long	    usbProduct = kProductID;
    sig_t	    oldHandler;
 
	vpnx_io_t	io_packet;
	
    vpnx_io_t   *io_from_usb;
    vpnx_io_t   *io_from_tcp;
    vpnx_io_t   *io_to_usb;
    vpnx_io_t   *io_to_tcp;

    progname = *argv++;
    argc--;
    
    loglevel = 0;
    remote_host[0] = '\0';
    port = 22;
    secure = false;
    mode = VPNX_CLIENT;
    result = 0;
    
	vpnx_set_log_level(loglevel);

	while (argc > 0 && ! result)
    {
        arg = *argv++;
        argc--;
        
        if (arg[0] == '-')
        {
            argdex = 1;
            
            while (arg[argdex] != '\0')
            {
                switch (arg[argdex++])
                {
                case 'c':
                    mode = VPNX_CLIENT;
                    break;
                case 's':
                    mode = VPNX_SERVER;
                    break;
                case 't':
                    secure = true;
                    break;
                case 'p':
                    if (arg[argdex] >= '0' && arg[argdex] <= '9')
                    {
                        usbProduct = strtoul(arg + argdex, NULL, 0);
                        while (arg[argdex] != '\0')
                        {
                            argdex++;
                        }
                    }
                    else if (argc > 0)
                    {
                        usbProduct = strtoul(*argv, NULL, 0);
                        argc--;
                        argv++;
                    }
                    else
                    {
                        return useage(progname);
                    }
                    break;
                case 'a':
                case 'r':
                    if (arg[argdex] >= '0' && arg[argdex] <= '9')
                    {
                        port = strtoul(arg + argdex, NULL, 0);
                        while (arg[argdex] != '\0')
                        {
                            argdex++;
                        }
                    }
                    else if (argc > 0)
                    {
                        port = strtoul(*argv, NULL, 0);
                        argc--;
                        argv++;
                    }
                    else
                    {
                        return useage(progname);
                    }
                    break;
                case 'v':
                    if (arg[argdex] >= '0' && arg[argdex] <= '9')
                    {
                        usbVendor = strtoul(arg + argdex, NULL, 0);
                        while (arg[argdex] != '\0')
                        {
                            argdex++;
                        }
                    }
                    else if (argc > 0)
                    {
                        usbVendor = strtoul(*argv, NULL, 0);
                        argc--;
                        argv++;
                    }
                    else
                    {
                        return useage(progname);
                    }
                    break;
                case 'l':
                    if (arg[argdex] >= '0' && arg[argdex] <= '9')
                    {
                        loglevel = (int)strtoul(arg + argdex, NULL, 0);
                        while (arg[argdex] != '\0')
                        {
                            argdex++;
                        }
                    }
                    else if (argc > 0)
                    {
                        loglevel = (int)strtoul(*argv, NULL, 0);
                        argc--;
                        argv++;
                    }
                    else
                    {
                        return useage(progname);
                    }
                    break;
				case 'h':
					useage(progname);
					return 0;
                default:
                    return useage(progname);
                }
            }
        }
        else
        {
            strncpy(remote_host, arg, sizeof(remote_host) - 1);
            remote_host[sizeof(remote_host) - 1] = '\0';
        }
    }
    
	vpnx_set_log_level(loglevel);

	// sanity check args
    //
    if (mode == VPNX_CLIENT)
    {
        ;
    }
    else
    {
        ;
    }
    // Set up a signal handler so we can clean up when we're interrupted from the command line
    // Otherwise we stay in our run loop forever.
	//
    oldHandler = signal(SIGINT, SignalHandler);
    if (oldHandler == SIG_ERR)
	{
        vpnx_log(0, "Could not establish new signal handler\n");
	}
        
    vpnx_log(1, "Setting USB device vendor ID=%ld and product ID=%ld\n", usbVendor, usbProduct);

    // setup
    //
    result = 0;
    s_usbd = -1;
	
	if (s_usbd < 0)
	{		
        // Open USB device
        //
        result = usb_open_device(usbVendor, usbProduct);
		if (result)
		{
			vpnx_log(0, "No USB device, FATAL Error\n");
		}
		else
		{
			vpnx_run_loop_init(mode, (void*)s_usbd, remote_host, port, port);
		}
	}
	while (! result)
	{
		result = vpnx_run_loop_slice();
	}
    if (s_usbd >= 0)
    {
        close(s_usbd);
    }
    vpnx_log(1, "%s Ends\n", progname);
    return 0;
}

