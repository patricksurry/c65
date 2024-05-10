# Detect if running on native Windows:
ifeq ($(OS),Windows_NT)
	CCFLAGS += -D WINDOWS_NATIVE
endif

all: c65

c65: c65.c io.c io.h fake65c02.h
	gcc -o c65 c65.c io.c $(CCFLAGS)

clean:
	rm -f *.o c65 c65.exe
