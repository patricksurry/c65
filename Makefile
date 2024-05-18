# Detect if running on native Windows:
ifeq ($(OS),Windows_NT)
	CCFLAGS += -D WINDOWS_NATIVE
endif

CSRC = c65.c magicio.c monitor.c linenoise.c
CHDR = c65.h magicio.h monitor.h linenoise.h fake65c02.h

all: c65

c65: $(CSRC) $(CHDR)
	gcc -Wunused $(CSRC) -o c65 $(CCFLAGS)

clean:
	rm -f *.o c65 c65.exe
