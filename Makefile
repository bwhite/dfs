
CC = gcc
CFLAGS = -Wall -D_FILE_OFFSET_BITS=64 -g -O0 `libgcrypt-config --cflags`
UNAME := $(shell uname)
LIBS =   -lpthread -ltpl `libgcrypt-config --libs` -lexpat 

ifeq ($(UNAME),Darwin)
LIBS +=  -lfuse_ino64 -L/opt/local/lib
CFLAGS += -DDARWIN -I/opt/local/include
endif

ifeq ($(UNAME),Linux)
LIBS +=  -lfuse
CFLAGS += -DLINUX
endif

TARGETS = tag dfs extent printLog logServer test keys

HEADERS =  dfs.h utils.h comm.h log.h tuple.h chits.h 

UTILS = utils.o comm.o cry.o chits.o

all: $(TARGETS)


tag:
	etags *.c *.h

extent: extent.o comm.o utils.o comm.o tuple.o cry.o
	$(CC) -o $@ $(CFLAGS) $^ $(LIBS)

extent.o: extent.c $(HEADERS)

logServer: logServer.o comm.o utils.o tuple.o cry.o xml.o chits.o
	$(CC) -o $@ $(CFLAGS) $^ $(LIBS)

logServer.o: logServer.c $(HEADERS)

keys: keys.o utils.o tuple.o cry.o chits.o xml.o
	$(CC) -o $@ $(CFLAGS) $^ $(LIBS)

keys.o: keys.c $(HEADERS)

dfs: dfs.o utils.o comm.o log.o tuple.o cry.o chits.o xml.o
	$(CC) -o $@ $(CFLAGS) $^ $(LIBS)

dfs.o: dfs.c $(HEADERS)

chits.o: chits.c $(HEADERS)

xml.o: xml.c $(HEADERS)

test2: test2.o chits.o xml.o utils.o cry.o
	gcc -g -o $@ $(CFLAGS) test2.o chits.o xml.o utils.o cry.o $(LIBS)

test2.o: test2.c $(HEADERS)

test: test.o chits.o xml.o utils.o cry.o
	gcc -g -o $@ $(CFLAGS) test.o chits.o xml.o utils.o cry.o $(LIBS)

test.o: test.c $(HEADERS)

tuple.o: tuple.c $(HEADERS)

printLog: printLog.o utils.o tuple.o cry.o
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


