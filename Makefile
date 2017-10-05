CC = gcc
CFLAGS = -g -Wall -I.
EXECS = oss user

all: $(EXECS)

clean:
	rm -f *.o $(EXECS)