all: c65

prof65: c65.c
	gcc -o prof65 -lreadline c65.c

clean:
	rm -f *.o c65
