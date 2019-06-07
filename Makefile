CFLAGS=-O0 -I /usr/local
CC=gcc

TARGETS=sender receiver sender_debug receiver_debug

UTILS=util.o

all: $(TARGETS) $(DEBUGTARGETS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

%_debug.o: %.c
	$(CC) $(CFLAGS) -DDEBUG -c $< -o $@

$(TARGETS): %:%.o util.o
	$(CC) $(CFLAGS) $^ -o $@


.PHONY:	clean

clean:
	rm -f *.o $(HELPERS) $(TARGETS)
