.RECIPEPREFIX = >
CC = powerpc64-linux-gnu-gcc
CFLAGS = -O2 -Wall
LDFLAGS =

all: EBOOT.BIN

EBOOT.BIN: main.c
> $(CC) $(CFLAGS) main.c -o EBOOT.BIN $(LDFLAGS)

clean:
> rm -f EBOOT.BIN