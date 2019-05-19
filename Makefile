CFLAGS=-O0 -I /usr/local
CC=gcc

TARGETS=sender receiver
UTILS=util.o

all: $(TARGETS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<


$(TARGETS): %:%.o util.o
	$(CC) $(CFLAGS) $^ -o $@



run:
	./sender

.PHONY:	clean

clean:
	rm *.o $(HELPERS) $(TARGETS)
