# Makefile pour compiler le projet
CC = gcc
CFLAGS = -Wall -pthread

all: server agency

server: server.c common.h
	$(CC) $(CFLAGS) -o server server.c

agency: agency.c common.h
	$(CC) $(CFLAGS) -o agency agency.c

clean:
	rm -f server agency *.o