all: c65

c65: c65.c
	gcc -o c65 -lreadline c65.c

clean:
	rm -f *.o c65
