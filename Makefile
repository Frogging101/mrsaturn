objects = saturnd.o netlinkcom.o

CC = gcc
CPPFLAGS = -isystem /usr/include/libnl3
LDLIBS = -lnl-3 -lnl-genl-3

saturnd: $(objects)

saturnd.o: saturnd.c netlinkcom.h
netlinkcom.o: netlinkcom.c netlinkcom.h

clean:
	rm -f *.o a.out
