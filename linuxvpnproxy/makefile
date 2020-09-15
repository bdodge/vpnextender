
#CROSS_COMPILE=arm-linux-gnueabihf-
CROSS_COMPILE=
CROSS_CC=$(CROSS_COMPILE)gcc

CFLAGS=-g -DLinux -I../common

HEADERS=../common/vpnextender.h ../common/vpnxtcp.h

vpnextender: vpnxprtproxy.o vpnxtcp.o vpnextender.o
	$(CROSS_CC) -o $@ $(CFLAGS) $^ -lusb

vpnxprtproxy.o: ../common/vpnxprtproxy.c $(HEADERS)
	$(CROSS_CC) -c $(CFLAGS) -o $@ $<

vpnextender.o: ../common/vpnextender.c $(HEADERS)
	$(CROSS_CC) -c $(CFLAGS) -o $@ $<

vpnxtcp.o: ../common/vpnxtcp.c  $(HEADERS)
	$(CROSS_CC) -c $(CFLAGS) -o $@ $<

