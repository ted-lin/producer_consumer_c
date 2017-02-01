CFLAGS = -Wall -O3

all:
	gcc ${CFLAGS} main.c -o mp -lpthread

debug:
	gcc ${CFLAGS} -DRB_DEBUG=1 main.c -o mp -lpthread

clean:
	rm -rf *.o mp
