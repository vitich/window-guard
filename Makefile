CC = gcc
CFLAGS = -Wall -O2
LIBS = -lX11 -lXrandr

all: window-guard

window-guard: src/window-guard.c
	$(CC) $(CFLAGS) -o window-guard src/window-guard.c $(LIBS)

clean:
	rm -f window-guard
