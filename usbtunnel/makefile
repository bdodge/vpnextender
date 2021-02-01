
ifndef TARGET_BOARD
	TARGET_BOARD=rpi0
#	TARGET_BOARD=rpi3
#	TARGET_BOARD=rpi4
#	TARGET_BOARD=bbb
endif

CFLAGS=-O2 -DLinux -DTUNNEL_BUILD -DTARGET_BOARD=$(TARGET_BOARD) -I../common -I../usbtunnel

# most systems have h/w float
# but not the raspberry pi zero

ifeq ($(TARGET_BOARD),rpi0)
	CROSS_COMPILE=arm-linux-gnueabi-
	CFLAGS+=-marm -march=armv6  -mfloat-abi=soft
else
	CROSS_COMPILE=arm-linux-gnueabihf-
endif

CROSS_CC=$(CROSS_COMPILE)gcc


HEADERS=tunnelsettings.h ../common/vpnextender.h ../common/vpnxtcp.h

usbtunnel: usbtunnel.o tunnelsettings.o vpnxtcp.o vpnextender.o
	$(CROSS_CC) -o $@ $(CFLAGS) $^

usbtunnel.o: usbtunnel.c $(HEADERS)
	$(CROSS_CC) -c $(CFLAGS) -o $@ $<

tunnelsettings.o: tunnelsettings.c $(HEADERS)
	$(CROSS_CC) -c $(CFLAGS) -o $@ $<

vpnextender.o: ../common/vpnextender.c $(HEADERS)
	$(CROSS_CC) -c $(CFLAGS) -o $@ $<

vpnxtcp.o: ../common/vpnxtcp.c  $(HEADERS)
	$(CROSS_CC) -c $(CFLAGS) -o $@ $<

clean:
	rm -rf *.o usbtunnel
	
