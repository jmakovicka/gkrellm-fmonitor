VERSION = 2.0.4
PKGNAME = gkrellm-fmonitor

CFLAGS = -O2 -Wall -fPIC
CC = gcc

SRCS = fmonitor.c
OBJS = fmonitor.o
SOBJ = fmonitor.so


$(SOBJ): $(OBJS)
	$(CC) -shared `pkg-config gtk+-2.0 --libs` $(OBJS) -o $(SOBJ)

install:
	cp $(SOBJ) /usr/local/lib/gkrellm2/plugins

clean:
	rm -f *.o core *.so* *.bak *~


fmonitor.o: fmonitor.c
	$(CC) $(CFLAGS) `pkg-config gtk+-2.0 --cflags` -c $(SRCS) -o $(OBJS)

dist:
	rm -rf $(PKGNAME)-$(VERSION)
	mkdir $(PKGNAME)-$(VERSION)
	cp Makefile README fmonitor.c fm_led.xpm $(PKGNAME)-$(VERSION)/
	tar zcf $(PKGNAME)-$(VERSION).tgz $(PKGNAME)-$(VERSION)
	rm -rf $(PKGNAME)-$(VERSION)
