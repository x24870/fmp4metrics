CC=gcc
OS=$(shell uname | tr '[:upper:]' '[:lower:]')
COMMIT_HASH=$(shell git rev-parse HEAD | cut -c -16)
BUILD_TIME="$(shell date)"

CFLAGS := -flto -O3
CFLAGS += -s
CFLAGS += -Wall
CFLAGS += -DLOG_LEVEL=5
CFLAGS += -I. -I./libfmp4
CFLAGS += -DCOMMIT_HASH=$(COMMIT_HASH) -DBUILD_TIME=$(BUILD_TIME)

LDFLAGS := -s -L./libfmp4
ifeq ($(OS),linux)
	LDFLAGS += -O3 -flto
	LDFLAGS += -Wl,--whole-archive -lfmp4 -Wl,--no-whole-archive
	LDFLAGS += -lrtmp -lavformat -lavcodec -lavutil -lwebsockets -lz -luv -lev -lcap -lssl -lcrypto
else ifeq ($(OS),darwin)
	LDFLAGS += -L/usr/local/opt/openssl/lib
	LDFLAGS += -all_load
	LDFLAGS += -lrtmp -lavformat -lavcodec -lavutil -lwebsockets -lz -luv -lev -lssl -lcrypto
endif

OBJS = main.o \
	   metric.o \
	   frames_per_second.o \
	   frame_interarrival_time.o \
	   media_stream_bitrate.o \
	   q2q_stream_latency.o

.PHONY: all libfmp4 linux darwin clean

all: $(OS)

%.o: %.c %.h
	$(CC) -c -o $@ $(CFLAGS) $<

libfmp4:
	$(MAKE) -C libfmp4

linux: libfmp4 $(OBJS)
	$(CC) -static -o $(BIN) $(OBJS) $(LDFLAGS)

darwin: libfmp4 $(OBJS)
	$(CC) -o $(BIN) $(OBJS) libfmp4/*.o libfmp4/cJSON/cJSON.o $(LDFLAGS)

clean:
	$(MAKE) -C libfmp4 clean
	rm -f *.o
	rm -f $(BIN)


