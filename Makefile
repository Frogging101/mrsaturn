objects = saturnd.o netlinkcom.o md.o algsocket.o

CC = gcc
CPPFLAGS = -g -Wall -isystem /usr/include/libnl3 -std=c99
LDLIBS = -lnl-3 -lnl-genl-3 -ludev

saturnd: $(objects)

algsocket.o: algsocket.c
md.o: md.c algsocket.h md.h saturnd.h
netlinkcom.o: netlinkcom.c netlinkcom.h md.h saturnd.h
saturnd.o: saturnd.c netlinkcom.h algsocket.h

clean:
	rm -f *.o a.out
