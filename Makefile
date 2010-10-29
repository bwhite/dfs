
CC = gcc
CFLAGS = -D_FILE_OFFSET_BITS=64 -g -O0
UNAME := $(shell uname)

ifeq ($(UNAME),Darwin)
LIBS =  -lfuse_ino64 -L/opt/local/lib -lgcrypt -lpthread -ltpl
CFLAGS += -DDARWIN -I/opt/local/include
endif

ifeq ($(UNAME),Linux)
LIBS =  -lfuse -lgcrypt -lpthread -ltpl
CFLAGS += -DLINUX
endif

TARGETS = dfs extent printLog logServer tuple_test

HEADERS =  dfs.h utils.h comm.h log.h tuple.h

all: $(TARGETS)


extent: extent.o comm.o utils.o comm.o tuple.o
	$(CC) -o $@ $(CFLAGS) $^ $(LIBS)

extent.o: extent.c $(HEADERS)

logServer: logServer.o comm.o utils.o tuple.o
	$(CC) -o $@ $(CFLAGS) $^ $(LIBS)

logServer.o: logServer.c $(HEADERS)


tuple_test: tuple_test.o tuple.o utils.o comm.o
	$(CC) -o $@ $(CFLAGS) $^ $(LIBS)

tuple_test.o: tuple_test.c $(HEADERS)

dfs: dfs.o utils.o comm.o log.o tuple.o
	$(CC) -o $@ $(CFLAGS) $^ $(LIBS) `pkg-config --cflags --libs protobuf`

dfs.o: dfs.c $(HEADERS)

tuple.o: tuple.c $(HEADERS)

printLog: printLog.o utils.o tuple.o
	$(CC) -o $@ $(CFLAGS) $^ $(LIBS)

printLog.o: printLog.c

log.o: log.c $(HEADERS)

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


