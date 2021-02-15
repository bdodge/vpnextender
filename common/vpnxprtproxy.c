
#include "vpnextender.h"
#include "vpnxtcp.h"

#ifdef Windows
#include "tchar.h"
#include "winusb.h"
#include "winusbio.h"
#elif defined(Linux)
  #ifdef LIBUSB01
    #include <usb.h>    // libusb-dev must be installed!
  #else
    #include <libusb-1.0/libusb.h>
  #endif
  #define LIBUSBSYNC 1 //  use synchtcstronous IO. I found no benefit to the async API
#include <signal.h>
#elif defined(OSX)
#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#endif

typedef struct usb_desc
{
    bool                    in_use;
    int                     rep;                ///<  read end point number
    int                     wep;                ///< write end point number
    int                     iep;                ///< interrupt end point number

    uint32_t                readPacketSize;     ///< bulk io max packet size (read)
    uint32_t                writePacketSize;    ///< bulk io max packet size (write)

    vpnx_io_t               tx_usb_packet;      ///< buffer for sync Tx  I/O
    vpnx_io_t               rx_usb_packet[2];   ///< buffer for async Tx  I/O (ping pong)
    int                     rx_usb_count[2];    ///< incremental count of rx bytes
    bool                    rx_completed[2];    ///< rx completed state
    bool                    rx_started[2];      ///< rx started state
    int                     rx_pong;            ///< which rx buffer is being read
	
#ifdef Windows
    HANDLE                  hio_wr;
    HANDLE                  hio_rd;
    HANDLE                  hio_ev;
    OVERLAPPED              rx_overlap[2];
#elif defined(Linux)
  #ifdef LIBUSB01
    struct usb_dev_handle  *hio;
    int                     busn;
    int                     devn;
  #else
	libusb_device_handle   *hio;
   #ifndef LIBUSBSYNC
	struct libusb_transfer *rx_usb_xfer[2];			///< usb transfer for async i/o
   #endif
  #endif
    int                     inum;
#elif defined(OSX)
    io_object_t             notification;
    IOUSBDeviceInterface    **deviceInterface;
    IOUSBInterfaceInterface **usbInterface;
    CFStringRef             deviceName;
    UInt32                  locationID;
#endif
}
usb_desc_t;

/// THE USB device
///
static usb_desc_t *gusb_device;

static usb_desc_t *create_usb_desc(void)
{
    usb_desc_t *desc;
    
    desc = (usb_desc_t *)MALLOC(sizeof(usb_desc_t));
    if (! desc)
    {
        vpnx_log(0, "Can't alloc usb desc\n");
        return NULL;
    }
    memset(desc, 0, sizeof(usb_desc_t));
    desc->in_use = true;
    return desc;
}

static void destroy_usb_desc(usb_desc_t *desc)
{
    if (desc)
    {
        FREE(desc);
    }
}

#ifdef Windows
int find_usb_device(long vid, long pid)
{
    // Windows usb (setupapi)
    const GUID                     *portGUID;
    BOOL                            rv;
    HDEVINFO                        hDev;
    SP_DEVICE_INTERFACE_DATA        devIntData;
    SP_DEVICE_INTERFACE_DETAIL_DATA *devIntDetail;

    DWORD   cbNeeded;
    BYTE    *ditBuf;
    DWORD   ditBufSize;
    TCHAR   portName[MAX_PATH];
    TCHAR   portEpName[MAX_PATH];
    int     index;
    int     err;

    TCHAR  vidstr[32], pidstr[32];
    int    dvid, dpid;
    
    portGUID = &s_guid_printer;
    portName[0] = _T('\0');

    ditBufSize = 65536;
    ditBuf = (LPBYTE)malloc(ditBufSize);
    if (!ditBuf)
    {
        vpnx_log(0, "Can't alloc device info buffer\n");
        return -1;
    }
    // setup api, get class devices: list all devices belonging to the interface
    // or setup class that are present
    //
    hDev = SetupDiGetClassDevs(
                                    portGUID, 
                                    NULL,
                                    NULL, 
                                    DIGCF_PRESENT | DIGCF_INTERFACEDEVICE
                              );
    if(hDev == INVALID_HANDLE_VALUE)
    {
        err = GetLastError();
        vpnx_log(0, "usb find: No Device:%d\n", err);
        free(ditBuf);
        return err;
    }

    index = 0;

    do
    {
        // enumerate the device interfaces in the set created above
        //
        devIntData.cbSize = sizeof(devIntData);
        rv = SetupDiEnumDeviceInterfaces(hDev, NULL, portGUID, index++, &devIntData);
       
        if(rv)
        {
            devIntDetail = (PSP_DEVICE_INTERFACE_DETAIL_DATA)ditBuf;
            devIntDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
            
            rv  = SetupDiGetDeviceInterfaceDetail(hDev, &devIntData, devIntDetail, ditBufSize, &cbNeeded, NULL);
            
            if(rv)
            {
                LPTSTR ps;

                // extract vid and pid from device path
                // &devIntDetail->DevicePath;
                //
                vidstr[0] = '\0';
                pidstr[0] = '\0';
                dvid = 0;
                dpid = 0;

                if((ps = _tcsstr(devIntDetail->DevicePath, L"vid_")) != NULL)
                {
                    dvid = _tcstoul(ps+4, NULL, 16);
                    _sntprintf(vidstr, 32, _T("%d"), vid );
                }
                if((ps = _tcsstr(devIntDetail->DevicePath, _T("pid_"))) != NULL)
                {
                    dpid = _tcstoul(ps+4, NULL, 16);
                    _sntprintf(pidstr, 32, _T("%d"), pid);
                }
                if (dvid == vid && dpid == pid)
                {
                    gusb_device = create_usb_desc();

                    vpnx_log(2, "Found device with matching vid/pid\n");
                    _tcsncpy(portName, devIntDetail->DevicePath, sizeof(portName)/sizeof(TCHAR) - 1);
                    portName[sizeof(portName)/sizeof(TCHAR) - 1] = '\0';
                    break;
                }
                index++;
            }
            else
            {
                vpnx_log(1, "Can't get detail\n");
            }
        }
        else
        {
            vpnx_log(4, "No devices of type\n");
            err = GetLastError();
        }
    }
    while(rv /*&& (index != (nDevice + 1))*/);
    
    free(ditBuf);
    SetupDiDestroyDeviceInfoList(hDev);
    
    if (portName[0] == '\0')
    {
        return -1;
    }
    gusb_device->hio_rd = INVALID_HANDLE_VALUE;
    gusb_device->hio_wr = INVALID_HANDLE_VALUE;

    gusb_device->wep = 1;
    gusb_device->rep = 2;

    gusb_device->readPacketSize = 512;
    gusb_device->writePacketSize = 512;

    // open bulk channel port
    //
    _sntprintf(portEpName, MAX_PATH, _T("%s\\%d"), portName, gusb_device->rep);

    gusb_device->hio_rd = CreateFile(
                            portName, 
                            GENERIC_READ | GENERIC_WRITE, 
                            FILE_SHARE_READ | FILE_SHARE_WRITE, 
                            (LPSECURITY_ATTRIBUTES)NULL,
                            OPEN_EXISTING, 
                            FILE_FLAG_OVERLAPPED,
                            NULL
                        );
    if(gusb_device->hio_rd == INVALID_HANDLE_VALUE)
    {
        vpnx_log(0, "Can't opne read endpoint\n");
        return -1;
    }
    if (gusb_device->rep != gusb_device->wep)
    {
        // open control channel port
        //
        _sntprintf(portEpName, MAX_PATH, _T("%s\\%d"), portName, gusb_device->wep);

        gusb_device->hio_wr = CreateFile(
                                portName, 
                                GENERIC_READ | GENERIC_WRITE, 
                                FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                (LPSECURITY_ATTRIBUTES)NULL,
                                OPEN_EXISTING, 
                                FILE_FLAG_OVERLAPPED,
                                NULL
                            );
        if(gusb_device->hio_wr == INVALID_HANDLE_VALUE)
        {
            vpnx_log(0, "Can't open write endpoint\n");
            CloseHandle(gusb_device->hio_rd);
            gusb_device->hio_rd = INVALID_HANDLE_VALUE;
            return -1;
        }
    }
    else
    {
        gusb_device->hio_wr = gusb_device->hio_rd;
    }
    vpnx_log(1, "Opened USB device vid=%X pid=%X\n", vid, pid);
    return 0;
}
#endif

#ifdef Linux

#ifndef LIBUSB01
static libusb_context *s_libusb_ctx = NULL;
#endif

int find_usb_device(long vid, long pid)
{
    // Linux libusb
    static int isinit = 0;
    int result = -1;

#ifdef LIBUSB01
    struct usb_bus    *bus;
    struct usb_device *dev;
    
    int devn, busn;
    
    if(! isinit)
    {
        usb_init();
        isinit = 1;
    }
    usb_find_busses();
    usb_find_devices();

    for(bus = usb_busses, busn = 0; bus; bus = bus->next, busn++)
    {
        for (dev = bus->devices, devn = 0; dev; dev = dev->next, devn++)
        {
            vpnx_log(4,
                    "pollusb dev %d vid:%x pid:%x cls:%d sc:%d\n",
                    devn,
                    dev->descriptor.idVendor, dev->descriptor.idProduct,
                    dev->descriptor.bDeviceClass, dev->descriptor.bDeviceSubClass
                    );
            if(dev->descriptor.bDeviceClass == 0 && dev->descriptor.idVendor != 0)
            {
                int c, i, a;
                
                vpnx_log(4, "\npoll_usb: found Device %x:%x\n",
                        dev->descriptor.idVendor, dev->descriptor.idProduct);

                for(c = 0; c < dev->descriptor.bNumConfigurations; c++)
                {
                    for(i = 0; i < dev->config[c].bNumInterfaces; i++)
                    {
                        for(a = 0; a < dev->config[c].interface[i].num_altsetting; a++)
                        {
                            vpnx_log(3, "poll_usb: found Device %x:%x  class:%x iface:%d  ifaceClass:%x\n",
                                    dev->descriptor.idVendor, dev->descriptor.idProduct, dev->descriptor.bDeviceClass, 
                                    i, dev->config[c].interface[i].altsetting[a].bInterfaceClass);

                            if(dev->config[c].interface[i].altsetting[a].bInterfaceClass == USB_CLASS_PRINTER)
                            {
                                int n, rep, wep, iep;
                                uint16_t rpz, wpz;
                                
                                rep = 0x81;
                                wep = 0;
                                iep = 0x82;
                                
                                vpnx_log(3, "Found Device %x:%x  class:%x iface:%d  ifaceClass:%x\n",
                                        dev->descriptor.idVendor, dev->descriptor.idProduct, dev->descriptor.bDeviceClass, 
                                        i, dev->config[c].interface[i].altsetting[a].bInterfaceClass);

                                for(n = 0; n < dev->config[c].interface[i].altsetting[a].bNumEndpoints; n++)
                                {
                                    switch(dev->config[c].interface[i].altsetting[a].endpoint[n].bmAttributes & USB_ENDPOINT_TYPE_MASK)
                                    {
                                    case USB_ENDPOINT_TYPE_BULK:
                                        switch(dev->config[c].interface[i].altsetting[a].endpoint[n].bEndpointAddress & USB_ENDPOINT_DIR_MASK)
                                        {
                                        case USB_ENDPOINT_IN:
                                            rep = dev->config[c].interface[i].altsetting[a].endpoint[n].bEndpointAddress;
                                            rpz = dev->config[c].interface[i].altsetting[a].endpoint[n].wMaxPacketSize;
                                            break;
                                        case USB_ENDPOINT_OUT:
                                            wep = dev->config[c].interface[i].altsetting[a].endpoint[n].bEndpointAddress;
                                            wpz = dev->config[c].interface[i].altsetting[a].endpoint[n].wMaxPacketSize;
                                            break;
                                        }
                                        break;
                                    case USB_ENDPOINT_TYPE_INTERRUPT:
                                        switch(dev->config[c].interface[i].altsetting[a].endpoint[n].bEndpointAddress & USB_ENDPOINT_DIR_MASK)
                                        {
                                        case USB_ENDPOINT_IN:
                                            iep = dev->config[c].interface[i].altsetting[a].endpoint[n].bEndpointAddress;
                                            break;
                                        }
                                        break;
                                    default:
                                        break;
                                    }
                                }
                                
                                if (dev->descriptor.idVendor == vid && dev->descriptor.idProduct == pid)
                                {
                                    vpnx_log(1, "Found USB tunnel device\n");
                                    gusb_device = create_usb_desc();
                                    gusb_device->rep = rep;
                                    gusb_device->wep = wep;
                                    gusb_device->iep = iep;
                                    gusb_device->writePacketSize = wpz;
                                    gusb_device->readPacketSize = rpz;
                                    gusb_device->busn = busn;
                                    gusb_device->devn = devn;
                                    gusb_device->inum = dev->config[c].interface[i].altsetting[a].bInterfaceNumber;
                                    gusb_device->hio = usb_open(dev);
                                    if(!gusb_device->hio)
                                    {
                                        destroy_usb_desc(gusb_device);
                                        gusb_device = NULL;
                                        vpnx_log(0, "Can't open usb device\n");
                                        return -1;
                                    }                               
                                    result = usb_claim_interface(gusb_device->hio, gusb_device->inum);
                                    if(result < 0)
                                    {
                                        vpnx_log(0, "Can't claim USB interface %d [%s]\n",
                                                            gusb_device->inum, strerror(-result));
                                        destroy_usb_desc(gusb_device);
                                        gusb_device = NULL;
                                        return result;
                                    }
                                    vpnx_log(2, "Opened USB device vid=%X pid=%X\n", vid, pid);
                                    vpnx_log(5, "  rep %d (%d max)  wep %d (%d max)\n", rep, rpz, wep, wpz);
                                    return 0;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
#else /* use 1.0 version not 0.1 version */
	struct libusb_device_descriptor devdesc;
	struct libusb_config_descriptor *confdesc;
	libusb_device_handle *handle;
	libusb_device *device;
	int usbll;
	int config;
	
    if(! isinit)
    {
		libusb_init(&s_libusb_ctx);
        isinit = 1;
  	
		usbll = vpnx_get_log_level();
		usbll -= 3;
		if (usbll > 5)
		{
			usbll = 4;
		}
		else if (usbll < 0)
		{
			usbll = 0;
		}
		libusb_set_option(s_libusb_ctx, LIBUSB_OPTION_LOG_LEVEL, usbll /*LIBUSB_LOG_LEVEL_DEBUG*/);

		gusb_device = NULL;
	}

	handle = libusb_open_device_with_vid_pid(s_libusb_ctx, vid, pid);
	if (!handle)
	{
		vpnx_log(0, "Can't open usb device\n");
		return -1;
	}
	device = libusb_get_device(handle);
	if (!device)
	{
		vpnx_log(0, "Can't get device\n");
		libusb_close(handle);
		return -1;
	}
	result = libusb_get_config_descriptor(device, config, &confdesc);
	if (result)
	{
		vpnx_log(0, "Can't get config descriptor\n");
		libusb_close(handle);
		return -1;
	}
	result = libusb_get_device_descriptor(device, &devdesc);
	if (result)
	{
		vpnx_log(0, "Can't get device descriptor\n");
		libusb_close(handle);
		return -1;
	}
	for (config = 0;  config < devdesc.bNumConfigurations; config++)
	{
		int i, a;
		
		result = libusb_get_config_descriptor(device, config, &confdesc);
		if (result)
		{
			vpnx_log(0, "Can't get config descriptor for config %d\n", config);
			libusb_close(handle);
			return -1;
		}
		for (i = 0; i < confdesc->bNumInterfaces; i++)
		{
		    for (a = 0; a < confdesc->interface[i].num_altsetting; a++)
		    {
		        vpnx_log(3, "poll_usb: iface:%d  ifaceClass:%x\n",
		                i, confdesc->interface[i].altsetting[a].bInterfaceClass);
		
		        if(confdesc->interface[i].altsetting[a].bInterfaceClass == USB_CLASS_PRINTER)
		        {
		            int n, rep, wep, iep;
		            uint16_t rpz, wpz;
		            
		            rep = 0x81;
		            wep = 0;
		            iep = 0x82;
		            
		            vpnx_log(3, "Found iface:%d  ifaceClass:%x\n", 
		                    i, confdesc->interface[i].altsetting[a].bInterfaceClass);
		
		            for(n = 0; n < confdesc->interface[i].altsetting[a].bNumEndpoints; n++)
		            {
		                switch(confdesc->interface[i].altsetting[a].endpoint[n].bmAttributes & LIBUSB_ENDPOINT_ADDRESS_MASK)
		                {
		                case LIBUSB_TRANSFER_TYPE_BULK:
		                    switch(confdesc->interface[i].altsetting[a].endpoint[n].bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK)
		                    {
		                    case LIBUSB_ENDPOINT_IN:
		                        rep = confdesc->interface[i].altsetting[a].endpoint[n].bEndpointAddress;
		                        rpz = confdesc->interface[i].altsetting[a].endpoint[n].wMaxPacketSize;
		                        break;
		                    case LIBUSB_ENDPOINT_OUT:
		                        wep = confdesc->interface[i].altsetting[a].endpoint[n].bEndpointAddress;
		                        wpz = confdesc->interface[i].altsetting[a].endpoint[n].wMaxPacketSize;
		                        break;
		                    }
		                    break;
		                case LIBUSB_TRANSFER_TYPE_INTERRUPT:
		                    switch(confdesc->interface[i].altsetting[a].endpoint[n].bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK)
		                    {
		                    case LIBUSB_ENDPOINT_IN:
		                        iep = confdesc->interface[i].altsetting[a].endpoint[n].bEndpointAddress;
		                        break;
		                    }
		                    break;
		                default:
		                    break;
		                }
		            }
	                gusb_device = create_usb_desc();
	                gusb_device->rep = rep;
	                gusb_device->wep = wep;
	                gusb_device->iep = iep;
	                gusb_device->writePacketSize = wpz;
	                gusb_device->readPacketSize = rpz;
	                gusb_device->inum = confdesc->interface[i].altsetting[a].bInterfaceNumber;
	                gusb_device->hio = handle;
	                vpnx_log(5, "  rep %d (%d max)  wep %d (%d max)\n", rep, rpz, wep, wpz);
	                break;
		        }
		    }
		}
		libusb_free_config_descriptor(confdesc);
	}
	
	if (gusb_device)
	{
		vpnx_log(1, "Found USB tunnel device\n");
	
		result = libusb_claim_interface(handle, gusb_device->inum);
		if(result < 0)
		{
			vpnx_log(0, "Can't claim USB interface [%s]\n", strerror(-result));
			libusb_close(handle);
			destroy_usb_desc(gusb_device);
			gusb_device = NULL;
			return result;
		}
		vpnx_log(2, "Opened USB device vid=%X pid=%X\n", vid, pid);
	}
#endif
    return result;
}

#ifndef LIBUSBSYNC
void usb_read_complete(struct libusb_transfer *xfer)
{
    usb_desc_t *dev = (usb_desc_t *)xfer->user_data;
    uint32_t  rc = (uint32_t)xfer->actual_length;

    // determine which buffer was being read (should be same as pong!)
    //
    if (!dev->rx_started[dev->rx_pong])
    {
        vpnx_log(0, "Expected usb rx to buffer %d, but not started?\n", dev->rx_pong);
    }
    dev->rx_started[dev->rx_pong] = false;
                          
    vpnx_log(4, "usb_read_complete [%d] with %d more bytes, payload is %d\n",
                    dev->rx_pong, rc, dev->rx_usb_packet[dev->rx_pong].count);

    if (xfer->status != LIBUSB_TRANSFER_COMPLETED)
    {
		if (xfer->status != LIBUSB_TRANSFER_TIMED_OUT)
		{
	        vpnx_log(0, "Error from asynchronous bulk read %d\n", xfer->status);
		}
		// no data moved
        vpnx_log(4, "Asynchronous bulk read err %d\n", xfer->status);
        return;
    }
    // count this chunk towards packet
    //
    dev->rx_usb_count[dev->rx_pong] += rc;
    
    if (dev->rx_usb_count[dev->rx_pong] >= VPNX_PACKET_SIZE/*dev->rx_usb_packet[dev->rx_pong].count*/)
    {
        // have gotten enough to complete the packet, all done
        //
        vpnx_log(4, "usb packet [%d] complete\n", dev->rx_pong);
        dev->rx_completed[dev->rx_pong] = true;
        dev->rx_usb_count[dev->rx_pong] = 0;
    }
}
#endif /* syncio */
#endif /* Linux */

#ifdef OSX

static IONotificationPortRef    gNotifyPort;
static io_iterator_t            gAddedIter;
static CFRunLoopRef             gRunLoop;

//  This routine will get called whenever any kIOGeneralInterest notification happens.  We are
//  interested in the kIOMessageServiceIsTerminated message so that's what we look for.  Other
//  messages are defined in IOMessage.h.
//
void DeviceNotification(void *refCon, io_service_t service, natural_t messageType, void *messageArgument)
{
    kern_return_t kr;
    usb_desc_t *privateDataRef = (usb_desc_t *)refCon;
    
    if (messageType == kIOMessageServiceIsTerminated)
    {
        vpnx_log(0, "Device removed.\n");
    
        // Dump our private data to stderr just to see what it looks like.
        vpnx_log(0, "privateDataRef->deviceName: ");
        CFShow(privateDataRef->deviceName);
        vpnx_log(0, "privateDataRef->locationID: 0x%lx.\n\n", (unsigned long)privateDataRef->locationID);
    
        // Free the data we're no longer using now that the device is going away
        CFRelease(privateDataRef->deviceName);
        
        if (privateDataRef->deviceInterface)
        {
            kr = (*privateDataRef->deviceInterface)->Release(privateDataRef->deviceInterface);
        }
        kr = IOObjectRelease(privateDataRef->notification);
        
        free(privateDataRef);
    }
}

//  This routine is the callback for our IOServiceAddMatchingNotification.  When we get called
//  we will look at all the devices that were added and we will:
//
//  1.  Create some private data to relate to each device (in this case we use the service's name
//      and the location ID of the device
//  2.  Submit an IOServiceAddInterestNotification of type kIOGeneralInterest for this device,
//      using the refCon field to store a pointer to our private data.  When we get called with
//      this interest notification, we can grab the refCon and access our private data.
//
void DeviceAdded(void *refCon, io_iterator_t iterator)
{
    kern_return_t       kr;
    io_service_t        usbDevice;
    IOCFPlugInInterface **plugInInterface = NULL;
    SInt32              score;
    HRESULT             res;
    
    while ((usbDevice = IOIteratorNext(iterator)))
    {
        io_name_t       deviceName;
        CFStringRef     deviceNameAsCFString;   
        usb_desc_t      *privateDataRef = NULL;
        UInt32          locationID;
        
        vpnx_log(1, "Device added.\n");
        
        // Create a local usb device descriptor to remmber this device
        //
        privateDataRef = create_usb_desc();
        if (! privateDataRef)
        {
            kr = IOObjectRelease(usbDevice);
            break;
        }
        
        // Get the USB device's name.
        kr = IORegistryEntryGetName(usbDevice, deviceName);
        if (KERN_SUCCESS != kr)
        {
            deviceName[0] = '\0';
        }
        
        deviceNameAsCFString = CFStringCreateWithCString(kCFAllocatorDefault, deviceName, 
                                                         kCFStringEncodingASCII);
        
        // Dump our data to stderr just to see what it looks like.
        vpnx_log(0, "deviceName: ");
        CFShow(deviceNameAsCFString);
        
        // Save the device's name to our private data.        
        privateDataRef->deviceName = deviceNameAsCFString;
                                                
        // Now, get the locationID of this device. In order to do this, we need to create an IOUSBDeviceInterface 
        // for our device. This will create the necessary connections between our userland application and the 
        // kernel object for the USB Device.
        kr = IOCreatePlugInInterfaceForService(usbDevice, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID,
                                               &plugInInterface, &score);

        if ((kIOReturnSuccess != kr) || !plugInInterface) {
            vpnx_log(0, "IOCreatePlugInInterfaceForService returned 0x%08x.\n", kr);
            kr = IOObjectRelease(usbDevice);
            continue;
        }
       
        // Use the plugin interface to retrieve the device interface.
        res = (*plugInInterface)->QueryInterface(plugInInterface, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),
                                                 (LPVOID*) &privateDataRef->deviceInterface);
        
        (*plugInInterface)->Release(plugInInterface);

        if (res || privateDataRef->deviceInterface == NULL) {
            vpnx_log(0, "QueryInterface returned %d.\n", (int) res);
            kr = IOObjectRelease(usbDevice);
            continue;
        }

        // Now that we have the IOUSBDeviceInterface, we can call the routines in IOUSBLib.h.
        // In this case, fetch the locationID. The locationID uniquely identifies the device
        // and will remain the same, even across reboots, so long as the bus topology doesn't change.
        
        kr = (*privateDataRef->deviceInterface)->GetLocationID(privateDataRef->deviceInterface, &locationID);
        if (KERN_SUCCESS != kr) {
            vpnx_log(0, "GetLocationID returned 0x%08x.\n", kr);
            kr = IOObjectRelease(usbDevice);
            continue;
        }
        else {
            vpnx_log(0, "Location ID: 0x%lx\n\n", (long)locationID);
        }

        privateDataRef->locationID = locationID;
        
        // Register for an interest notification of this device being removed. Use a reference to our
        // private data as the refCon which will be passed to the notification callback.
        kr = IOServiceAddInterestNotification(gNotifyPort,                      // notifyPort
                                              usbDevice,                        // service
                                              kIOGeneralInterest,               // interestType
                                              DeviceNotification,               // callback
                                              privateDataRef,                   // refCon
                                              &(privateDataRef->notification)   // notification
                                              );
                                                
        if (KERN_SUCCESS != kr)
        {
            vpnx_log(0, "IOServiceAddInterestNotification returned 0x%08x.\n", kr);
            kr = IOObjectRelease(usbDevice);
            continue;
        }
        
        // use device reference to get interfaces
        IOUSBConfigurationDescriptorPtr config;
        IOUSBFindInterfaceRequest interfaceRequest;
        IOUSBInterfaceInterface** usbInterface;
        SInt32 score;
        uint8_t numConfig;
        IOReturn ret;

        // open the device using its interface
        //
        IOUSBDeviceInterface  **devIface = privateDataRef->deviceInterface;

        ret = (*devIface)->USBDeviceOpen(devIface);
        if (ret == kIOReturnSuccess)
        {
            kr = (*devIface)->GetNumberOfConfigurations(devIface, &numConfig);
            if (!numConfig)
            {
                vpnx_log(0, "No configs in device\n");
                kr = IOObjectRelease(usbDevice);
                continue;
            }
            vpnx_log(4, "Device has %d configuration(s)\n", numConfig);
            
            // set first configuration as active
            ret = (*devIface)->GetConfigurationDescriptorPtr(devIface, 0, &config);
            if (ret != kIOReturnSuccess)
            {
                vpnx_log(0, "Could not set active configuration (error: %x)\n", ret);
                kr = IOObjectRelease(usbDevice);
                continue;
            }
            else
            {
                (*devIface)->SetConfiguration(devIface, config->bConfigurationValue);
            }
        }
        else if (ret == kIOReturnExclusiveAccess)
        {
            // this is not a problem as we can still do some things
        }
        else
        {
            vpnx_log(1, "Could not open device (error: %x)\n", ret);
            kr = IOObjectRelease(usbDevice);
            continue;
        }
        // now iterate the usb interface and get the endpoints we want
        //
        interfaceRequest.bInterfaceClass    = kIOUSBFindInterfaceDontCare;
        interfaceRequest.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
        interfaceRequest.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
        interfaceRequest.bAlternateSetting  = kIOUSBFindInterfaceDontCare;
        int ifaceNo;
        io_service_t usbRef;
        
        (*devIface)->CreateInterfaceIterator(devIface,
                                            &interfaceRequest,
                                            &iterator
                                            );
        
        ifaceNo = 0;
        
        while ((usbRef = IOIteratorNext(iterator)) != 0)
        {
            IOCreatePlugInInterfaceForService(usbRef,
                                                kIOUSBInterfaceUserClientTypeID,
                                                kIOCFPlugInInterfaceID, &plugInInterface, &score);
            IOObjectRelease(usbRef);
            (*plugInInterface)->QueryInterface(plugInInterface,
                                        CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID),
                                        (LPVOID)&usbInterface);
            (*plugInInterface)->Release(plugInInterface);
           
            if (ret != kIOReturnSuccess)
            {
                vpnx_log(1, "Could not get usb interface %d (error: %x)\n", ifaceNo, ret);
                break;
            }
            //Get interface class and subclass
            uint8_t interfaceClass;
            uint8_t interfaceSubClass;
            uint8_t interfaceNumEndpoints;
            
            kr = (*usbInterface)->GetInterfaceClass(usbInterface, &interfaceClass);
            kr = (*usbInterface)->GetInterfaceSubClass(usbInterface, &interfaceSubClass);
            vpnx_log(5, "Interface %d class %d, subclass %d\n", ifaceNo, interfaceClass, interfaceSubClass);

            // [TODO] - exclude devices that arent the right class?
            
            //Get the number of endpoints associated with this interface
            kr = (*usbInterface)->GetNumEndpoints(usbInterface, &interfaceNumEndpoints);
            if (kr != kIOReturnSuccess)
            {
                vpnx_log(1, "Unable to get number of endpoints (%08x)\n", kr);
                (void) (*usbInterface)->USBInterfaceClose(usbInterface);
                (void) (*usbInterface)->Release(usbInterface);
                break;
            }
            vpnx_log(4, "Found %d end points on interface %d\n", interfaceNumEndpoints, ifaceNo);

            ret = (*usbInterface)->USBInterfaceOpen(usbInterface);
            if (ret != kIOReturnSuccess)
            {
                vpnx_log(1, "Could not open interface %d (error: %x)\n", ifaceNo, ret);
                (void) (*usbInterface)->Release(usbInterface);
            }
            else
            {
                uint8_t direction;
                uint8_t number;
                uint8_t xferType;
                uint16_t maxPacket;
                uint8_t interval;
                int pipe;
                
                vpnx_log(4, "Opened interface %d\n", ifaceNo);
                
                for (pipe = 1; pipe <= interfaceNumEndpoints; pipe++)
                {
                    kr = (*usbInterface)->GetPipeProperties(usbInterface, pipe, &direction, &number, &xferType, &maxPacket, &interval);
                    if (kr != kIOReturnSuccess)
                    {
                        vpnx_log(0, "Can't get interface %d pipe propertes for pipe %d\n", ifaceNo, pipe);
                        break;
                    }
                    vpnx_log(4, "Iface %d pipe %d: dir=%d xfer=%d maxpack=%d\n", ifaceNo, pipe, direction, xferType, maxPacket);
                    
                    if (direction == kUSBOut && xferType == kUSBBulk)
                    {
                        privateDataRef->wep = pipe;
                        privateDataRef->writePacketSize = maxPacket;
                    }
                    if (direction == kUSBIn && xferType == kUSBBulk)
                    {
                        privateDataRef->rep = pipe;
                        privateDataRef->readPacketSize = maxPacket;
                    }
                    if (xferType == kUSBInterrupt)
                    {
                        privateDataRef->iep = pipe;
                    }
                }
                // if a device was found that has everything we need, remember it
                //
                if (privateDataRef->rep && privateDataRef->wep)
                {
                    vpnx_log(2, "Found USB tunnel device\n");
                    privateDataRef->usbInterface = usbInterface;
                    gusb_device = privateDataRef;
                    break;
                }
                else
                {
                    (void) (*usbInterface)->Release(usbInterface);
                }
            }
        }
        // Done with this USB device; release the reference added by IOIteratorNext
        kr = IOObjectRelease(usbDevice);
        
        if (privateDataRef->rep && privateDataRef->wep)
        {
            // got a device, no need to iterate
            break;
        }
        else
        {
            destroy_usb_desc(privateDataRef);
        }
    }
}

int find_usb_device(long vid, long pid)
{
    CFMutableDictionaryRef  matchingDict;
    CFRunLoopSourceRef      runLoopSource;
    CFNumberRef             numberRef;
    kern_return_t           kr;

    // Set up the matching criteria for the devices we're interested in. The matching criteria needs to follow
    // the same rules as kernel drivers: mainly it needs to follow the USB Common Class Specification, pp. 6-7.
    // See also Technical Q&A QA1076 "Tips on USB driver matching on Mac OS X"
    // <http://developer.apple.com/qa/qa2001/qa1076.html>.
    // One exception is that you can use the matching dictionary "as is", i.e. without adding any matching
    // criteria to it and it will match every IOUSBDevice in the system. IOServiceAddMatchingNotification will
    // consume this dictionary reference, so there is no need to release it later on.
    //
    // Interested in instances of class IOUSBDevice and its subclasses
    //
    matchingDict = IOServiceMatching(kIOUSBDeviceClassName);
    if (matchingDict == NULL)
    {
        vpnx_log(0, "IOServiceMatching returned NULL.\n");
        return -1;
    }

    // We are interested in all USB devices (as opposed to USB interfaces).  The Common Class Specification
    // tells us that we need to specify the idVendor, idProduct, and bcdDevice fields, or, if we're not interested
    // in particular bcdDevices, just the idVendor and idProduct.  Note that if we were trying to match an
    // IOUSBInterface, we would need to set more values in the matching dictionary (e.g. idVendor, idProduct,
    // bInterfaceNumber and bConfigurationValue.

    // Create a CFNumber for the idVendor and set the value in the dictionary
    //
    numberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &vid);
    CFDictionarySetValue(matchingDict,
                         CFSTR(kUSBVendorID),
                         numberRef);
    CFRelease(numberRef);

    // Create a CFNumber for the idProduct and set the value in the dictionary
    //
    numberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pid);
    CFDictionarySetValue(matchingDict,
                         CFSTR(kUSBProductID),
                         numberRef);
    CFRelease(numberRef);
    numberRef = NULL;

    // Create a notification port and add its run loop event source to our run loop
    // This is how async notifications get set up.
    //
    gNotifyPort = IONotificationPortCreate(kIOMasterPortDefault);
    runLoopSource = IONotificationPortGetRunLoopSource(gNotifyPort);

    gRunLoop = CFRunLoopGetCurrent();
    CFRunLoopAddSource(gRunLoop, runLoopSource, kCFRunLoopDefaultMode);

    // Now set up a notification to be called when a device is first matched by I/O Kit.
    kr = IOServiceAddMatchingNotification(gNotifyPort,                  // notifyPort
                                         kIOFirstMatchNotification,     // notificationType
                                         matchingDict,                  // matching
                                         DeviceAdded,                   // callback
                                         NULL,                          // refCon
                                         &gAddedIter                    // notification
                                         );
                                            
    // Iterate once to get already-present devices and arm the notification
    //
    DeviceAdded(NULL, gAddedIter);
    return 0;
}

void usb_read_complete(void *refCon, IOReturn kr, void *arg0)
{
    usb_desc_t *dev = (usb_desc_t *)refCon;
    uint32_t  rc = (uint32_t)arg0;

    // determine which buffer was being read (should be same as pong!)
    //
    if (!dev->rx_started[dev->rx_pong])
    {
        vpnx_log(0, "Expected usb rx to buffer %d, but not started?\n", dev->rx_pong);
    }
    dev->rx_started[dev->rx_pong] = false;
                          
    vpnx_log(4, "usb_read_complete [%d] with %d more bytes, payload is %d\n",
                    dev->rx_pong, rc, dev->rx_usb_packet[dev->rx_pong].count);

    /*
    if (dev->rx_usb_count[dev->rx_pong] == 0)
    {
        vpnx_dump_packet("usb rx first packet\n", &dev->rx_usb_packet[dev->rx_pong], 5);
    }
    */
    if (kr != kIOReturnSuccess)
    {
        vpnx_log(0, "Error from asynchronous bulk read (%08x)\n", kr);
        return;
    }
    // count this chunk towards packet
    //
    dev->rx_usb_count[dev->rx_pong] += rc;
    
    if (dev->rx_usb_count[dev->rx_pong] >= VPNX_PACKET_SIZE/*dev->rx_usb_packet[dev->rx_pong].count*/)
    {
        // have gotten enough to complete the packet, all done
        //
        vpnx_log(4, "usb packet [%d] complete\n", dev->rx_pong);
        dev->rx_completed[dev->rx_pong] = true;
        dev->rx_usb_count[dev->rx_pong] = 0;
    }
}
#endif // OSX

int usb_open_device(long vid, long pid)
{
    static bool s_blurbed_none = false;
    int result;
    
    gusb_device = NULL;

    result = find_usb_device(vid, pid);
    if (result)
    {
        vpnx_log(s_blurbed_none ? 4 : 0, "Can't find any USB tunnel devices\n");
        s_blurbed_none = true;
        return result;
    }
    #ifdef OSX
    if (!gusb_device)
    {
        // no device found in intial setup, so run-loop until its added
        //
        CFRunLoopRun();
    }
    #endif
    if (gusb_device)
    {
        return 0;
    }
    return -1;
}

int usb_write(void *pdev, vpnx_io_t *io)
{
    usb_desc_t *dev = (usb_desc_t *)pdev;
    uint8_t *psend;
    int tosend;
    int sent;
    int wc;
    
    tosend = VPNX_PACKET_SIZE; //io->count + VPNX_HEADER_SIZE;
    psend = (uint8_t *)io;
    sent = 0;
    
    while (sent < tosend)
    {
#ifdef Windows
    BOOL        rv;
    int         err;
    int         timer;
    OVERLAPPED  overlap;
    DWORD       written;

    memset(&overlap, 0, sizeof(OVERLAPPED));
    rv = WriteFile(dev->hio_wr, (LPCVOID)(psend + sent), tosend - sent, &written, &overlap);
    for(timer = 0; ! rv && (timer < 5000); timer++)
    {
        rv = GetOverlappedResult(dev->hio_wr, &overlap, &written, FALSE);
        if(!rv)
        {
            err = GetLastError();
            if(err != ERROR_IO_INCOMPLETE && err != ERROR_IO_PENDING)
            {
                break;
            }
            Sleep(1);
        }
    }
    if (written < (DWORD)(tosend - sent))
    {
        vpnx_log(0, "usb write failed\n");
        CancelIo(dev->hio_wr);
    }
    wc = (int)written;
#elif defined(Linux)
	#ifdef LIBUSB01
        //vpnx_log(2, "usb gonna write %d\n", tosend - sent);
        wc = usb_bulk_write(dev->hio, dev->wep, (char*)psend + sent, tosend - sent, 5000);
        if (wc < 0)
        {
            vpnx_log(0, "usb write failed: %d\n", wc);
            return wc;
        }
        //vpnx_log(2, "usb wrote %d\n", wc);
        // flush
        usb_bulk_write(dev->hio, dev->wep, (char*)psend + sent, 0, 0);
	#else
		int result;
	
		result = libusb_bulk_transfer(dev->hio, dev->wep, (char*)psend + sent, tosend - sent, &wc, 5000);
		if (result)
		{
            vpnx_log(0, "usb write failed: %d\n", result);
            return result;
		}
		result = wc;
	#endif
#elif defined(OSX)
        wc = tosend - sent;
        if ((*dev->usbInterface)->WritePipe(dev->usbInterface, dev->wep, psend + sent, wc) != kIOReturnSuccess)
        {
            vpnx_log(0, "usb write failed\n");
            wc = -1;
            return -1;
        }
        if ((sent + wc) >= tosend)
        {
            // flush
            (*dev->usbInterface)->WritePipe(dev->usbInterface, dev->wep, psend, 0);
        }
#endif
        sent += wc;
    }
    return 0;
}

int usb_read(void *pdev, vpnx_io_t **io)
{
    usb_desc_t *dev = (usb_desc_t *)pdev;
    int result;
    
    result = 0;
	*io = NULL;
	
    // if the read for the current buffer is complete, return that and kick off another
    // asynchronous read
    //
    if (dev->rx_completed[dev->rx_pong])
    {
        vpnx_log(4, "usb read returning buf [%d] \n", dev->rx_pong);
        dev->rx_started[dev->rx_pong] = false;
        dev->rx_completed[dev->rx_pong] = false;
        dev->rx_usb_count[dev->rx_pong] = 0;
        *io = &dev->rx_usb_packet[dev->rx_pong];
        dev->rx_pong = dev->rx_pong ? 0 : 1;
    }
    // start another async read operation on current buffer if not started
    //
    if (! dev->rx_started[dev->rx_pong])
    {
        int have;
        int remaining;
        
        have = dev->rx_usb_count[dev->rx_pong];
        if (have > 0)
        {
            remaining = VPNX_PACKET_SIZE - have;
        }
        else
        {
            // read a full packet
            remaining = VPNX_PACKET_SIZE;
        }
        // always read multiples of actual transport packet size
        //
        /*
        remaining += dev->readPacketSize - 1;
        remaining /= dev->readPacketSize;
        remaining *= dev->readPacketSize;
        */
        vpnx_log(6, "usb read %sstarting async read on [%d] for %d\n", have ? "re-" : "", dev->rx_pong, remaining);
        
        dev->rx_started[dev->rx_pong] = true;
#ifdef Windows
        BOOL rv;
        DWORD gotten;

        memset(&dev->rx_overlap[dev->rx_pong], 0, sizeof(OVERLAPPED));

        rv = ReadFile(
                    dev->hio_rd,
                    (LPVOID)(((uint8_t*)&dev->rx_usb_packet[dev->rx_pong]) + have),
                    remaining,
                    &gotten,
                    &dev->rx_overlap[dev->rx_pong]
                    );
        if(! rv)
        {
            int err = GetLastError();

            if(err != ERROR_IO_INCOMPLETE && err != ERROR_IO_PENDING)
            {
                CancelIo(gusb_device->hio_rd);
                printf("Can't start read\n");
                return -1;
            }
        }
        vpnx_log(4, "usb read for [%d], got %u initialily\n", dev->rx_pong, gotten);
        dev->rx_usb_count[dev->rx_pong] = gotten;
#endif
#ifdef Linux
  #ifdef LIBUSBSYNC
	#ifdef LIBUSB01
        result = usb_bulk_read(
                                dev->hio,
                                dev->rep,
                                ((uint8_t*)&dev->rx_usb_packet[dev->rx_pong]) + have,
                                remaining,
                                have ? 2500 : 15 /* shorter timeout for first chunk */
                                );
        if (result == -ETIMEDOUT)
        {
            result = 0;
        }
        if (result < 0)
        {
            vpnx_log(0, "usb read failed: %d\n", result);
            return result;
        }
	#else
		int result;
		
		int rc;
		
		result = libusb_bulk_transfer(
								dev->hio,
								dev->rep,
								((uint8_t*)&dev->rx_usb_packet[dev->rx_pong]) + have,
                                remaining,
								&rc,
                                have ? 2500 : 15 /* shorter timeout for first chunk */
                                );
		if (result == LIBUSB_ERROR_TIMEOUT)
		{
			if (have)
			{
				vpnx_log(1, "usb read timedout\n");
			}
			else
			{
				result = 0;
			}
		}
		if (result)
		{
            vpnx_log(0, "usb read failed: %d\n", result);
            return result;
		}
		result = rc;
	#endif
		vpnx_log(result == 0 ? 6 : 5, "usb read %d more\n", result);
        if (result > 0)
        {
            dev->rx_usb_count[dev->rx_pong] += result;
            dev->rx_started[dev->rx_pong] = false;
            if (dev->rx_usb_count[dev->rx_pong] >= VPNX_PACKET_SIZE/*dev->rx_usb_packet[dev->rx_pong].count*/)
            {
                // have gotten enough to complete the packet, all done
                //
                vpnx_log(4, "usb packet [%d] complete\n", dev->rx_pong);
                dev->rx_completed[dev->rx_pong] = true;
                dev->rx_usb_count[dev->rx_pong] = 0;
            }
			else
			{
				vpnx_log(2, "incomplete packet [%d] %d only to make %d\n",
						dev->rx_pong, result, dev->rx_usb_count[dev->rx_pong]);
			}
        }
        else
        {
            dev->rx_started[dev->rx_pong] = false;
        }
  #else // async reads
		// libusb frees this itself 
		dev->rx_usb_xfer[dev->rx_pong] = libusb_alloc_transfer(0);
		
		libusb_fill_bulk_transfer(
								dev->rx_usb_xfer[dev->rx_pong],
								dev->hio,
								dev->rep,
								((uint8_t*)&dev->rx_usb_packet[dev->rx_pong]) + have,
                                remaining,
								usb_read_complete,
								dev,
                                15000
                                );
								
		result = libusb_submit_transfer(dev->rx_usb_xfer[dev->rx_pong]);
		if (result)
		{
            vpnx_log(0, "usb read failed: %d\n", result);
            return result;
		}
        dev->rx_started[dev->rx_pong] = true;		
  #endif
#endif
#ifdef OSX
        CFRunLoopSourceRef  runLoopSource;
        IOReturn kr;
        
        // To receive asynchronous I/O completion notifications, create an event source and
        // add it to the run loop
        //
        kr = (*dev->usbInterface)->CreateInterfaceAsyncEventSource(dev->usbInterface, &runLoopSource);

        if (kr != kIOReturnSuccess)
        {
            vpnx_log(0, "Unable to create asynchronous event source (%08x)\n", kr);
            return -1;
        }
        // add our event source to the run loop
        //
        CFRunLoopAddSource(CFRunLoopGetMain()/*CFRunLoopGetCurrent()*/, runLoopSource, kCFRunLoopDefaultMode);

        kr = (*dev->usbInterface)->ReadPipeAsync(
                                                 dev->usbInterface,
                                                 dev->rep,
                                                 ((uint8_t*)&dev->rx_usb_packet[dev->rx_pong]) + have,
                                                 remaining,
                                                 usb_read_complete,
                                                 (void*)dev
                                                );
        if (kr != kIOReturnSuccess)
        {
            vpnx_log(0, "Unable to perform asynchronous bulk read (%08x)\n", kr);
            return -1;
        }
#endif
    }
#ifdef Windows
    else
    {
        BOOL rv;
        DWORD gotten;

        rv = GetOverlappedResult(dev->hio_rd, &dev->rx_overlap[dev->rx_pong], &gotten, FALSE);
        if (!rv)
        {
            int err = GetLastError();

            if (err != ERROR_IO_INCOMPLETE && err != ERROR_IO_PENDING)
            {
                vpnx_log(0, "usb read error %d\n", err);
                CancelIo(gusb_device->hio_rd);
                return -1;
            }
            else
            {
                vpnx_log(4, "incomplete/pending io\n");
            }
            // read not complete, keep going
            return 0;
        }
        if ((gotten + dev->rx_usb_count[dev->rx_pong]) < VPNX_PACKET_SIZE)
        {
            // if nothing, restart the read
            //
            if (!gotten)
            {
                CancelIo(dev->hio_rd);
                dev->rx_started[dev->rx_pong] = false;
                return 0;
            }
            vpnx_log(3, "usb read [%d] in progress, got %u so far\n", dev->rx_pong, gotten);
            // not complete yet
            return 0;
        }
        vpnx_log(3, "usb_read_complete [%d] with %d more bytes, payload is %d\n",
            dev->rx_pong, gotten, dev->rx_usb_packet[dev->rx_pong].count);
        dev->rx_completed[dev->rx_pong] = true;
        dev->rx_started[dev->rx_pong] = false;
    }
#endif
    return 0;
}

#ifdef OSX
static void run_loop_timer_callback(CFRunLoopTimerRef timer, void *info)
{
    int result;
    
    result = vpnx_run_loop_slice();
}
#endif

int vpnx_run_loop()
{
    int result = 0;
    
#ifndef OSX
    // call the slice function repeatedly
    //
    do
    {
        result = vpnx_run_loop_slice();
#if defined(Linux) && !defined(LIBUSBYSNC)
	// drive libusb's internal state and it will callback our read complete callback when ready
	{
	struct timeval timeout;
	int completed = 0;
	
	// there is another short rest in tcp read select, but this short one helps a bit in cpu
	timeout.tv_sec = 0;
	timeout.tv_usec = 2000;
	libusb_handle_events_timeout_completed(s_libusb_ctx, &timeout, &completed);
	}
#endif
    }
    while (!result);
#else
    CFRunLoopRef runLoop = CFRunLoopGetCurrent();
    CFRunLoopTimerContext context = {0, NULL, NULL, NULL, NULL};
    CFRunLoopTimerRef timer = CFRunLoopTimerCreate(kCFAllocatorDefault, 0.01, 0.01, 0, 0, run_loop_timer_callback, &context);
    CFRunLoopAddTimer(runLoop, timer, kCFRunLoopDefaultMode);

    // call the slice function on a timer inside the main run loop
    //
    CFRunLoopRun();
#endif
    return result;
}
    
void SignalHandler(int sigraised)
{
    vpnx_log(0, "\nInterrupted.\n");
   
    exit(0);
}

static const char *s_extender_status;

const char *vpnx_extender_status(void)
{
    return s_extender_status ? s_extender_status : "Not Initialized";
}

#ifndef VPNX_GUI

// VPNPRTPROXY is being built for command-line / console use
//

static int useage(const char *progname)
{
    vpnx_log(0, "Usage: %s -c [-h][-v VID][-p PID][-l loglevel][-r remote-port][remote-host]\n", progname);
    vpnx_log(0, "   or: %s -s -a local-port [h][-v VID][-p PID][-l loglevel][-r remote-port][remote-host]\n\n", progname);
    vpnx_log(0, "  -c LAN->VPN client\n");
    vpnx_log(0, "       A USB connection is translated to a TCP connection to a remote host\n");
    vpnx_log(0, "       provided in the USB connection request. If the request does not contain\n");
    vpnx_log(0, "       a hostname, the remtoe-host/remote-port provided on the command line is used\n");
    vpnx_log(0, "    -h This help text\n");
    vpnx_log(0, "    -v look for this vendor id (VID) for the USB tunnel (default is 0x3F0)\n");
    vpnx_log(0, "    -p look for this this product id (PID) for the USB tunnel (default is 0x102)\n");
    vpnx_log(0, "    -r default port on remote host to access\n");
    vpnx_log(0, "    remote-host, if supplied, is the default remote host to connect to when a USB\n");
    vpnx_log(0, "    connection is initiated by the prt proxy and the request doesn't contain a host name\n");
    vpnx_log(0, "\n");
    vpnx_log(0, "       Example:\n");
    vpnx_log(0, "         %s -c -r22 vpnbasedhost\n", progname); 
    vpnx_log(0, "\n");
    vpnx_log(0, "       This will make ssh connections from a LAN based host to a host on your VPN possible\n"); 
    vpnx_log(0, "       using a command line (on the LAN host) like:\n");
    vpnx_log(0, "\n");
    vpnx_log(0, "         ssh -p 1234 username@usbtunnelhost\n"); 
    vpnx_log(0, "\n");
    vpnx_log(0, "       where usbtunnelhost is the USB tunnel and 1234 is the port the USB tunnel is serving on\n");
    vpnx_log(0, "\n");
    vpnx_log(0, "  -s VPN->LAN server\n");
    vpnx_log(0, "       Listen on local-port for TCP connections and translate them to\n");
    vpnx_log(0, "       USB connections to the USB tunnel, and then marshall TCP traffic via USB\n");
    vpnx_log(0, "       to the tunnel which will translate back TCP traffic on the LAN\n");
    vpnx_log(0, "    -a local port to listen on for TCP connections\n");
    vpnx_log(0, "    -r port on LAN remote host to access\n");
    vpnx_log(0, "    remote-host, if supplied, is remote host on LAN to connect to when a TCP connection\n");
    vpnx_log(0, "    is made to local-port. If not supplied, the default host configured for the USB tunnel is used\n");
    vpnx_log(0, "\n");
    vpnx_log(0, "       Example:\n");
    vpnx_log(0, "         %s -s -a2222 -r22 lanbasedhost\n", progname); 
    vpnx_log(0, "\n");
    vpnx_log(0, "       This will make ssh connections to landbasedhost on your LAN possible\n"); 
    vpnx_log(0, "       using a command line like:\n");
    vpnx_log(0, "\n");
    vpnx_log(0, "         ssh -p 2222 username@localhost\n"); 
    vpnx_log(0, "\n");
    return -1;
}

int main(int argc, const char *argv[])
{
    int         mode;
    char        remote_hosts[VPNX_MAX_PORTS][VPNX_MAX_HOST];
    uint16_t    remote_ports[VPNX_MAX_PORTS];
    uint16_t    local_ports[VPNX_MAX_PORTS];
    bool        secure;
    int         loglevel;
    const char *progname;
    int         argdex;
    const char *arg;
	char       *nextnum;
    int         result;
    int			rp_conns;
	int			lp_conns;
	int			host_conns;
	
    unsigned    usbVendor = kVendorID;
    unsigned    usbProduct = kProductID;
#ifndef Windows
    sig_t       oldHandler;
#endif
	
    progname = *argv++;
    argc--;
    
    loglevel = 0;
    secure = false;
    mode = VPNX_CLIENT;
    result = 0;

	for (rp_conns = 0; rp_conns < VPNX_MAX_PORTS; rp_conns++)
	{
	    remote_hosts[rp_conns][0] = '\0';
	    remote_ports[rp_conns] = 0;
	    local_ports[rp_conns] = 0;
	}	
	rp_conns = 0;
	lp_conns = 0;
	host_conns = 0;
	
    s_extender_status = "Initializing";
    
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
                        usbProduct = (unsigned)strtoul(arg + argdex, NULL, 0);
                        while (arg[argdex] != '\0')
                        {
                            argdex++;
                        }
                    }
                    else if (argc > 0)
                    {
                        usbProduct = (unsigned)strtoul(*argv, NULL, 0);
                        argc--;
                        argv++;
                    }
                    else
                    {
                        return useage(progname);
                    }
                    break;
                case 'a':
					if (lp_conns >= VPNX_MAX_PORTS)
					{
						vpnx_log(0, "Too many (%d) local ports specified\n", lp_conns + 1);
						lp_conns--;
					}
                    if (arg[argdex] >= '0' && arg[argdex] <= '9')
                    {
                        local_ports[lp_conns++] = (uint16_t)strtoul(arg + argdex, &nextnum, 0);
                        while (arg[argdex] != '\0')
                        {
                            argdex++;
                        }
                    }
                    else if (argc > 0)
                    {
                        local_ports[lp_conns++] = (uint16_t)strtoul(*argv, &nextnum, 0);
                        argc--;
                        argv++;
                    }
                    else
                    {
                        return useage(progname);
                    }
                    break;
                case 'r':
					if (rp_conns >= VPNX_MAX_PORTS)
					{
						vpnx_log(0, "Too many (%d) remote ports specified\n", rp_conns + 1);
						lp_conns--;
					}
                    if (arg[argdex] >= '0' && arg[argdex] <= '9')
                    {
                        remote_ports[rp_conns++] = (uint16_t)strtoul(arg + argdex, &nextnum, 0);
                        while (arg[argdex] != '\0')
                        {
                            argdex++;
                        }
                    }
                    else if (argc > 0)
                    {
                        remote_ports[rp_conns++] = (uint16_t)strtoul(*argv, &nextnum, 0);
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
                        usbVendor = (uint16_t)strtoul(arg + argdex, &nextnum, 0);
                        while (arg[argdex] != '\0')
                        {
                            argdex++;
                        }
                    }
                    else if (argc > 0)
                    {
                        usbVendor = (uint16_t)strtoul(*argv, &nextnum, 0);
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
                        loglevel = (int)strtoul(arg + argdex, &nextnum, 0);
                        while (arg[argdex] != '\0')
                        {
                            argdex++;
                        }
                    }
                    else if (argc > 0)
                    {
                        loglevel = (int)strtoul(*argv, &nextnum, 0);
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
			if (host_conns >= VPNX_MAX_PORTS)
			{
				vpnx_log(0, "Too many (%d) hosts specified\n", host_conns + 1);
				host_conns--;
			}
            strncpy(&remote_hosts[host_conns][0], arg, VPNX_MAX_HOST - 1);
            remote_hosts[host_conns++][VPNX_MAX_HOST - 1] = '\0';
        }
    }

    vpnx_set_log_level(loglevel);
    
    // sanity check args
    //
    if (mode == VPNX_CLIENT)
    {
        if (lp_conns == 0)
		{
			// default local connection port, if not specied
			//
			local_ports[0] = 2222;
			remote_ports[0] = 22;
			lp_conns++;
		}
    }
    else
    {
        if (lp_conns == 0)
		{
			// default local connection port, if not specied
			//
			local_ports[0] = 2222;
			lp_conns = 1;
		}
        if (rp_conns == 0)
		{
			// default remotes connection port, if not specied
			//
			remote_ports[0] = 22;
			rp_conns = 1;
		}
        if (lp_conns != rp_conns)
		{
			vpnx_log(0, "Need to specify same number of local and remote ports\n");
			return -1;
		}
    }

#ifndef Windows
    // Set up a signal handler so we can clean up when we're interrupted from the command line
    // Otherwise we stay in our run loop forever.
    //
    oldHandler = signal(SIGINT, SignalHandler);
    if (oldHandler == SIG_ERR)
    {
        vpnx_log(0, "Could not establish new signal handler.");
    }
#endif
    vpnx_log(1, "%s Mode\n", (mode == VPNX_SERVER) ? "Server" : "Client");
    vpnx_log(2, "Looking for devices matching VendorID=%04x and ProductID=%04x.\n", usbVendor, usbProduct);

    // setup
    //
    s_extender_status = "Looking for extender device";
    result = 0;
    
    do //try
    {
        if (! gusb_device)
        {
            // Poll for the USB device. It must be present and running before we can do anything, so this blocks
            //
            result = usb_open_device(usbVendor, usbProduct);
            if (result)
            {
                vpnx_log(0, "Can't open usb device\n");
                break;
            }
            else
            {
				const char *remotes[VPNX_MAX_PORTS];
				const char *last_host = NULL;
				int i;
				
				// assume unspecified remote hosts should use last specified remote hosts
				//
				for (i = 0; i < VPNX_MAX_PORTS; i++)
				{
					if (i < host_conns && remote_hosts[i][0])
					{
						remotes[i] = &remote_hosts[i][0];
						last_host = remotes[i];
					}
					else
					{
						remotes[i] = last_host;
					}
				}				
				vpnx_run_loop_init(mode, (void*)gusb_device, remotes, remote_ports, local_ports);
            }
        }
    }
    while (0); //catch
    
    if (!result)
    {
        // run the main loop
        //
        s_extender_status = "Running";
        vpnx_run_loop();
    }
    if (gusb_device)
    {
        destroy_usb_desc(gusb_device);
    }
    vpnx_log(1, "%s Ends\n", progname);
    return 0;
}
#else

// VPNPRTPROXY is being built as a GUI application
//
// See GUI file specific to OS/Windowing system for "main"
//
static enum { gsInit, gsUsbSearch, gsUsbOpened, gsRun } g_state = gsInit;
static char     x_remote_hosts[VPNX_MAX_PORTS][VPNX_MAX_HOST];
static uint16_t x_remote_ports[VPNX_MAX_PORTS];
static uint16_t x_vid;
static uint16_t x_pid;
static uint16_t x_local_ports[VPNX_MAX_PORTS];
static int      x_mode;

int vpnx_gui_init(
              bool isserver,
              const char *remote_hosts,
              const uint16_t remote_ports[VPNX_MAX_PORTS],
              const uint16_t vid,
              const uint16_t pid,
              const uint16_t local_ports[VPNX_MAX_PORTS],
              uint32_t log_level,
              void (*logging_func)(const char *)
              )
{
    int i;
    int j;

    s_extender_status = "Initializing";

    for (i = 0; i < VPNX_MAX_PORTS; i++)
    {
        strncpy(&x_remote_hosts[i][0], remote_hosts, VPNX_MAX_HOST - 1);
        x_remote_hosts[i][VPNX_MAX_HOST - 1] = '\0';
        for (j = 0; j < VPNX_MAX_HOST; j++)
        {
            if (x_remote_hosts[i][j] == ',' || x_remote_hosts[i][j] == '\0')
            {
                x_remote_hosts[i][j] = '\0';
                break;
            }
        }
        remote_hosts += j;
        if (remote_hosts[0] == ',')
        {
            remote_hosts++;
        }
        x_remote_ports[i] = remote_ports[i];
        x_local_ports[i]  = local_ports[i];
    }
    x_vid = vid;
    x_pid = pid;
    x_mode = isserver ? VPNX_SERVER : VPNX_CLIENT;
    
    s_extender_status = "Looking for extender device";
    gusb_device = NULL;
    g_state = gsInit;
    
    if (logging_func)
    {
        vpnx_set_log_function(logging_func);
    }
    vpnx_set_log_level(log_level);
    
    return 0;
}

int vpnx_gui_slice(void)
{
    int result;
    const char *remotes[VPNX_MAX_PORTS];
    int i;
    
    switch (g_state)
    {
        case gsInit:
            // kick off usb device finder
            result = usb_open_device(x_vid, x_pid);
            if (result < 0)
            {
#ifdef Windows
                WaitMessage();
#elif defined(Linux)
                usleep(10000);
#endif
                result = 0;
                break;
            }
            if (result)
            {
                vpnx_log(0, "Can't find any USB tunnel devices\n");
                return result;
            }
            g_state = gsUsbSearch;
            break;
            
        case gsUsbSearch:
            // wait for usb device to be found
            if (gusb_device)
            {
                s_extender_status = "Running";
                g_state = gsUsbOpened;
            }
            break;
            
        case gsUsbOpened:
            for (i = 0; i < VPNX_MAX_PORTS; i++)
            {
                remotes[i] = &x_remote_hosts[i][0];
            }
            vpnx_run_loop_init(x_mode, gusb_device, remotes, x_remote_ports, x_local_ports);
            g_state = gsRun;
            break;
        
        case gsRun:
            vpnx_run_loop_slice();
            break;
    }
    return 0;
}

#endif
