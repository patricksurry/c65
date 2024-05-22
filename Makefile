# Detect if running on native Windows:
ifeq ($(OS),Windows_NT)
	CCFLAGS += -D WINDOWS_NATIVE
endif

CSRC = c65.c magicio.c monitor.c linenoise.c
CHDR = c65.h magicio.h monitor.h linenoise.h fake65c02.h

all: c65 tests

c65: $(CSRC) $(CHDR)
	gcc -Wunused $(CSRC) -o c65 $(CCFLAGS)

tests: c65 tests/test.in
	./c65 -r tests/wozmon.rom -l tests/wozmon.sym < tests/test.in | perl -pe 's/\x1b\[[0-9;]*[mG]//g' > tests/test.out

clean:
	rm -f *.o c65 c65.exe
