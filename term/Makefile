CC = gcc
CFLAGS = -Wall -Wextra `pkg-config --cflags x11 cairo lua vterm`
LDFLAGS = `pkg-config --libs x11 cairo lua vterm` -lutil

term: main.c pty.c
	$(CC) $(CFLAGS) -o term main.c pty.c $(LDFLAGS)

clean:
	rm -f term
