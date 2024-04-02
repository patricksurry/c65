all: c65

c65: c65.c io.c io.h fake65c02.h
	gcc -o c65 c65.c io.c

clean:
	rm -f *.o c65
