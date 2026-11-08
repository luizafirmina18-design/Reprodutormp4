CC = ppu-gcc
CFLAGS = -O2 -Wall
LDFLAGS = -lnet -lsysutil -lsysmodule

all: EBOOT.BIN

EBOOT.BIN: main.c
	$(CC) $(CFLAGS) main.c -o EBOOT.BIN $(LDFLAGS)

clean:
	rm -f EBOOT.BIN