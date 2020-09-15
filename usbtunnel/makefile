
CROSS_COMPILE=arm-linux-gnueabihf-
CROSS_CC=$(CROSS_COMPILE)gcc

CFLAGS=-g -DLinux -I../common

app: usbtunnel

usbtunnel: usbtunnel.o vpnxtcp.o
	$(CROSS_CC) -o $@ $(CFLAGS) $^

usbtunnel.o: usbtunnel.c
	$(CROSS_CC) -c $(CFLAGS) -o $@ $<

vpnxtcp.o: ../common/vpnxtcp.c
	$(CROSS_CC) -c $(CFLAGS) -o $@ $<

