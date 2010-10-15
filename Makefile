
CC = gcc
CFLAGS = -D_FILE_OFFSET_BITS=64 -g -O0
UNAME := $(shell uname)

ifeq ($(UNAME),Darwin)
LIBS =  -lfuse_ino64 -L/opt/local/lib -lgcrypt -lpthread
CFLAGS += -DDARWIN -I/opt/local/include
endif

ifeq ($(UNAME),Linux)
LIBS =  -lfuse -lgcrypt -lpthread
CFLAGS += -DLINUX
endif

TARGETS = dfs extent printLog logServer

all: $(TARGETS)


extent: extent.o comm.o utils.o comm.o
	$(CC) -o $@ $(CFLAGS) $^ $(LIBS)

extent.o: extent.c dfs.h utils.h comm.h

logServer: logServer.o comm.o utils.o 
	$(CC) -o $@ $(CFLAGS) $^ $(LIBS)

logServer.o: logServer.c dfs.h utils.h log.h


# example for 'tsearch()'
ts: ts.o
	$(CC) -o $@ $(CFLAGS) $^ $(LIBS)

ts.o: ts.c dfs.h


dfs: dfs.o utils.o comm.o log.o
	$(CC) -o $@ $(CFLAGS) $^ $(LIBS)

dfs.o: dfs.c dfs.h utils.h comm.h log.h

printLog: printLog.o utils.o
	$(CC) -o $@ $(CFLAGS) $^ $(LIBS)

printLog.o: printLog.c dfs.h utils.h comm.h log.h

log.o: log.c dfs.h utils.h comm.h log.h

comm.o: comm.c comm.h utils.h

utils.o: utils.c utils.h

try: try_server try_client

try_server: try_server.o utils.o comm.o
	$(CC) -o $@ $(CFLAGS) $^ $(LIBS)

try_client: try_client.o utils.o comm.o
	$(CC) -o $@ $(CFLAGS) $^ $(LIBS)

try_client.o: try_client.c utils.h try.h

try_server.o: try_server.c utils.h try.h

dist:
	tar cvf starter.tar Makefile try*.c try.h dfs.c dfs.h comm.c comm.h utils.c utils.h 
clean:
	rm -f $(TARGETS) *.o *~ /tmp/extents


