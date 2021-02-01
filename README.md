# vpnextender

## VPN Extender

This is a system to allow you to selectively bridge a virtual private network
connected computer (VPN'd) to a system on a local area network (LAN).

Most VPNs lock down your PC/laptop and disallow any LAN access. The disable other
network adapters on your computer so even clever network setups won't work to allow
LAN access while conenected to your VPN. This is for security and is a good idea
to ensure rogue actors with access to your LAN don't also get access to a corporate
network via VPN.

Unfortunately for those who work-from-home and connect via VPN to a remote office,
but also need to access local network resource (like your networked printer at home
for example), this usually means stopping and restarting the VPN, or using USB keys,
etc.  It is even more troublesome for those that develop network attached devices
at home while VPN attached.

### History

This system was developed when I worked for a company that only had a MS Windows VPN
solution yet our development was all on Linux desktops. Even if we copied the source
code and bypassed the source control system, the compiler we used needed a license
and the license server was hosted in the office.  In order to work from home I needed
a solution to at least access the license server from a Linux desktop.

Long before the VPN was put in place we had an ssh server in the office that provided
port forwarding to various systems like the license server to allow us to work from home.
That ssh server was still in place, but only accessible behind the VPN of course.

### How it Works

After spending (too long) trying multiple interfaces and NAT, etc. on the Windows end,
I thought of using a non-network-adapter method of attaching a local network. The basic
concept was simple. Use an application on the Windows PC that could connect to a host on
the VPN, then for every chunk of TCP data sent/received on that connection, package it
on some other wire format to a remote system that can convert that wire format back
in to TCP/IP data on a different connection to a LAN system. As long as this middle
component reliably transfers raw data, it is simply just an extension of the network
cable as far as the two remote hosts are concerned.  What I needed was a wire-format
that was simple, fast, already built-in with no configuration or extra connectors
needed. USB was the obvious choice and since I had a lot of experience with USB
printer drivers, I built a "USB Printer" device that, as far as the Windows host
was concerned was just a run-of-the-mill older HP Inkjet that had in-box drivers that
would just install on Windows. The device was actually a Beagle-Bone-Blac (BBB) running
Linux and used the printer gadget driver to appear as a printer on the USB device port.

An application on the BBB waited for my LAN PC (the Linux desktop) to connect to its port
and then transferred any TCP/IP data to the Windows PC via the USB port.  Another
applicaton on the Window PC connected to the ssh server on the VPN and sent data 
to/from the USB "printer" port to/from that TCP/IP connection. As far as either 
side of the TCP/IP connection was concerned, they had a direct connection to
each other. On the Linux desktop, the only change needed was to change the host
name in the .ssh/config file to point at the BBB vs the old ssh server.  The Windows
app took the ssh server hostname as a command line argument.  After buffing the
bugs out, this system worked well and allowed me to work from home daily for year(s).
Since the BBB is waiting for the connection from the LAN host, this form
of operation I call "server mode".

### More history

Much later, my work focused on developing small network attached devices. At home,
while VPN connected to the office, I wanted to download code to my local devices,
ssh to them to get a console, etc. Instead of using multiple screens and copying
files I decided to use the same USB printer technique, just in reverse. The "Windows"
side is now a Macbook Pro and connects to the BBB via USB Printer the same way,
but the BBB now connects to the LAN host instead of the other way around.  The
Mac application listens on a port for connections and marshalls connected data
via USB to the BBB, which then sends it along to the LAN host. Now to avoid having
to deal with configuring the BBB as to which LAN host to connect to, etc. I added
controls so the PC app can tell it what to do.  Now from my VPN connected PC I
can ssh to a LAN host by simply "ssh user@localhost -p XXX" where XXX is the port
my PC application is listening on. This mode I called "client mode".

### And now

After adding commmand line switches to control client and server behavior, 
generalizing connections and code, and porting the "PC" side app to Windows,
Linux, and OSX, the vpnextender is now ready for general use.

## Building

The BBB application is in "usbtunnel".  Although it is targeted at BBB it
is simple and can run on any generic Linux device that has USB gadget 
support and a USB device or OTG port.  Just go to usbtunnel and "make".

Building the VPN host application is simple as well. Us the xcode project
for Mac, the makefile for Linux and the vscode project for Windows.

## Use Cases

### Basic

I'm a work-from-home-body and need to be on my VPN constantly to get emails
and join corporate Zoom. At the same time, I need to print the documents
we design so I can see how they look.

#### Solution

  You want to run the vpn extender in client mode. The extender will 
  connect to your printer and take data from your PC via the USB connection.
  
  On the usbtunnel (BBB) device, start the application in client mode
  
    > usbtunnel -c -r <printer queue port> <printer hostname or IP address>
    
    The printer queue port will depend on what kind of printer you have.
    If it is a new printer that has Airprint support, use the IPP port
    631. For really old printers, use the standard printer port of 9100.
    
  On your PC, start the proxy application. The port you pick here depends
  on if you have "root" access (to open port 631 for example) and want to
  keep your old printer configuration, or if you want to add your printer
  locally using a high port.  Lets say you don't want to be "root". Pick
  a port number like "43500" (anything under 65536 and over 1024 is OK).
  Start the PC proxy like:
  
    > vpnextender -s -a <port number>
    
  Now you can "Add Printer" selecting "localhost" and "port number" as
  the address of the printer, and then use that printer to print. Just
  remember to restart the BBB extender, and PC extender application
  if they reboot/repower/etc.
  
    