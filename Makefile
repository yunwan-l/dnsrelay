CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2
OBJS = dnsrelay.o dns_protocol.o domain_table.o relay_engine.o
LDLIBS =

ifeq ($(OS),Windows_NT)
LDLIBS += -lws2_32
endif

all: dnsrelay.exe

dnsrelay.exe: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

clean:
	rm -f *.o *.obj dnsrelay.exe
