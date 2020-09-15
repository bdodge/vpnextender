
#include "vpnextender.h"
#include "vpnxtcp.h"

#ifdef OSX
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
#elif defined(Linux)
    struct usb_dev_handle  *hio;
    int                     busn;
    int                     devn;
    int                     inum;
#elif defined(OSX)
    io_object_t				notification;
    IOUSBDeviceInterface	**deviceInterface;
    IOUSBInterfaceInterface **usbInterface;
    CFStringRef				deviceName;
    UInt32					locationID;
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
#endif

#ifdef Linux
int find_usb_device(long vid, long pid)
{
    // Linux libusb
    static int isinit = 0;

    struct usb_bus    *bus;
    struct usb_device *dev;
    
    int devn, busn;
    
    int result = -1;

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
									return 0;
								}
                            }
                        }
                    }
                }
            }
        }
    }
    return result;
}
#endif /* Linux */

#ifdef OSX

static IONotificationPortRef	gNotifyPort;
static io_iterator_t			gAddedIter;
static CFRunLoopRef				gRunLoop;

//	This routine will get called whenever any kIOGeneralInterest notification happens.  We are
//	interested in the kIOMessageServiceIsTerminated message so that's what we look for.  Other
//	messages are defined in IOMessage.h.
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

//	This routine is the callback for our IOServiceAddMatchingNotification.  When we get called
//	we will look at all the devices that were added and we will:
//
//	1.  Create some private data to relate to each device (in this case we use the service's name
//	    and the location ID of the device
//	2.  Submit an IOServiceAddInterestNotification of type kIOGeneralInterest for this device,
//	    using the refCon field to store a pointer to our private data.  When we get called with
//	    this interest notification, we can grab the refCon and access our private data.
//
void DeviceAdded(void *refCon, io_iterator_t iterator)
{
    kern_return_t		kr;
    io_service_t		usbDevice;
    IOCFPlugInInterface	**plugInInterface = NULL;
    SInt32				score;
    HRESULT 			res;
    
    while ((usbDevice = IOIteratorNext(iterator)))
	{
        io_name_t		deviceName;
        CFStringRef		deviceNameAsCFString;	
        usb_desc_t      *privateDataRef = NULL;
        UInt32			locationID;
        
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
        kr = IOServiceAddInterestNotification(gNotifyPort,						// notifyPort
											  usbDevice,						// service
											  kIOGeneralInterest,				// interestType
											  DeviceNotification,				// callback
											  privateDataRef,					// refCon
											  &(privateDataRef->notification)	// notification
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
                uint8_t data[512];
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
        vpnx_dump_packet("usb rx first packet\n", &dev->rx_usb_packet[dev->rx_pong], 3);
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
    
    if (dev->rx_usb_count[dev->rx_pong] >= dev->rx_usb_packet[dev->rx_pong].count)
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
    int result;
    
    gusb_device = NULL;
    
    result = find_usb_device(vid, pid);
    if (result)
    {
        vpnx_log(0, "Can't find any USB tunnel devices\n");
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
    int chunk;
    int result;
    
    tosend = io->count + VPNX_HEADER_SIZE;
    psend = (uint8_t *)io;
    sent = 0;
    result = 0;
    
    // break write into chunks of transport packet size
    //
    while (sent < tosend && result == 0)
    {
        chunk = dev->writePacketSize;
        if (chunk > (tosend - sent))
        {
            chunk = tosend - sent;
        }
#ifdef Linux
		result = usb_bulk_write(dev->hio, dev->wep, (char*)psend, chunk, 5000);
		if (result < 0)
		{
			vpnx_log(0, "usb write failed: %d\n", result);
			return result;
		}
		if (result != chunk)
		{
			chunk = result;
		}
		result = 0;
#endif
#ifdef OSX
        if ((*dev->usbInterface)->WritePipe(dev->usbInterface, dev->wep, psend, chunk) != kIOReturnSuccess)
        {
            result = -1;
        }
#endif
        psend += chunk;
        sent += chunk;
    }
    return 0;
}

int usb_read(void *pdev, vpnx_io_t **io)
{
    usb_desc_t *dev = (usb_desc_t *)pdev;
    int result;
    
    result = 0;

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
            remaining = dev->rx_usb_packet[dev->rx_pong].count - have;
            if (remaining > dev->readPacketSize)
            {
                if (remaining > VPNX_MAX_PACKET_BYTES)
                {
                    vpnx_log(0, "usb read dropping corrupt count buffer\n");
                    return -1;
                }
            }
            // always read a full packet
            //
            remaining = dev->readPacketSize;
        }
        else
        {
            remaining = dev->readPacketSize; //VPNX_HEADER_SIZE;
        }
        vpnx_log(5, "usb read %sstarting async read on [%d] for %d\n", have ? "re-" : "", dev->rx_pong, remaining);
        dev->rx_started[dev->rx_pong] = true;
#ifdef Linux
		result = usb_bulk_read(
								dev->hio,
								dev->rep,
								((uint8_t*)&dev->rx_usb_packet[dev->rx_pong]) + have,
								remaining,
								10 /* 10ms timeout */
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
		vpnx_log(2, "usb read %d more\n", result);
		if (result > 0)
		{
			dev->rx_usb_count[dev->rx_pong] += result;
	        dev->rx_started[dev->rx_pong] = false;
		    if (dev->rx_usb_count[dev->rx_pong] >= dev->rx_usb_packet[dev->rx_pong].count)
		    {
		        // have gotten enough to complete the packet, all done
		        //
		        vpnx_log(4, "usb packet [%d] complete\n", dev->rx_pong);
		        dev->rx_completed[dev->rx_pong] = true;
		        dev->rx_usb_count[dev->rx_pong] = 0;
		    }
		}
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
        CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource, kCFRunLoopDefaultMode);

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

static int useage(const char *progname)
{
    vpnx_log(0, "Usage: %s -c [-v VID][-p PID][-l loglevel][-r remote-port] [remote-host]\n", progname);
    vpnx_log(0, "      or  -s [-v VID][-p PID][-l loglevel] -r local-port\n\n");
    vpnx_log(0, "  -c provides an entry into a VPN from the LAN\n");
    vpnx_log(0, "     The remote-host/port will be connected to by the extender proxy\n");
    vpnx_log(0, "     running on the USB connected PC on the VPN when the USB SBC is\n");
    vpnx_log(0, "     connected to from the LAN at its host/port\n\n");
    vpnx_log(0, "  -s provides an entry into the LAN from a VPN\n");
    vpnx_log(0, "     The extender proxy on the USB connected VPN PC will listen on\n");
    vpnx_log(0, "     local-port for TCP connections which will translate to a connection\n");
    vpnx_log(0, "     from the USB SBC to a host/port on the LAN\n\n");
    vpnx_log(0, "  -v look for USB device with this vendor id (VID)\n");
    vpnx_log(0, "  -p look for USB device with this product id (PID)\n");
    //vpnx_log(0, "  -t use TLS for TCP connection to local port\n");
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
 
    progname = *argv++;
    argc--;
    
    loglevel = 0;
    remote_host[0] = '\0';
    port = 2222;
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
        vpnx_log(0, "Could not establish new signal handler.");
	}
        
    vpnx_log(2, "Looking for devices matching vendor ID=%ld and product ID=%ld.\n", usbVendor, usbProduct);

    // setup
    //
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
				vpnx_run_loop_init(mode, (void*)gusb_device, remote_host, port);
            }
        }
    }
    while (0); //catch
    
    if (!result)
    {
        // run the main loop
        //
        vpnx_run_loop();
    }
    if (gusb_device)
    {
        destroy_usb_desc(gusb_device);
    }
    vpnx_log(1, "%s Ends\n", progname);
    return 0;
}

