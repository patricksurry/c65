#include "fake65c02.h"
#include "io.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

uint8_t memory[65536];
int rws[65536];
int writes[65536];

int getc_addr, peekc_addr, putc_addr, blkio_addr, timer_addr;
int ticks = 0, mark = 0, shutdown = 0;

/*
blkio supports the following action values.  write the action value
after setting other parameters to initiate the action.  All actions
return 0x0 on success and nonzero (typically 0xff) otherwise.
A portable check for blkio is to write 0x1 to status, then write 0x0 to action
and check if status is now 0.
    0 - status: detect if blkio available, 0x0 if enabled, 0xff otherwise
    1 - read: read the 1024 byte block @ blknum to bufptr
    2 - write: write the 1024 byte block @ blknum from bufptr
    ff - exit: clean exit from the simulator, dumping profling data
*/
typedef struct BLKIO {
  uint8_t action;  // I: request an action (write after setting other params)
  uint8_t status;  // O: action status
  uint16_t blknum; // I: block to read or write
  uint16_t bufptr; // I/O: low-endian pointer to 1024 byte buffer to read/write
} BLKIO;

BLKIO *blkiop;
FILE *fblk = NULL;

uint8_t read6502(uint16_t addr) {
  char buf[1];
  int ch, delta;
  if (addr == getc_addr) {
    while (!_kbhit()) {
    }
    ch = _getc();
    shutdown = (ch == EOF);
    memory[addr] = (uint8_t)ch;
  } else if (addr == peekc_addr) {
    ch = _kbhit() ? (_getc() | 0x80) : 0;
    memory[addr] = (uint8_t)ch;
  } else if (addr == timer_addr /* start timer */) {
    mark = ticks;
  } else if (addr == timer_addr + 1 /* stop timer */) {
    delta = ticks - mark;
    memory[timer_addr + 2] = (uint8_t)((delta >> 16) & 0xff);
    memory[timer_addr + 3] = (uint8_t)((delta >> 24) & 0xff);
    memory[timer_addr + 4] = (uint8_t)((delta >> 0) & 0xff);
    memory[timer_addr + 5] = (uint8_t)((delta >> 8) & 0xff);
  }
  rws[addr] += 1;
  return memory[addr];
}

void write6502(uint16_t addr, uint8_t val) {
  char *line;
  int n;
  if (addr == putc_addr) {
    _putc(val);
  } else if (addr == blkio_addr) {
    blkiop->status = 0xff;
    if (fblk) {
      if (val < 3) {
        blkiop->status = 0;
        if (val == 1 || val == 2) {
          fseek(fblk, 1024 * blkiop->blknum, SEEK_SET);
          if (val == 1) {
            fread(memory + blkiop->bufptr, 1024, 1, fblk);
          } else {
            fwrite(memory + blkiop->bufptr, 1024, 1, fblk);
            fflush(fblk);
          }
        }
      }
    }
  }
  rws[addr] += 1;
  writes[addr] += 1;
  memory[addr] = val;
}

void show_cpu() {
  printf(
      "c65: PC=%04x A=%02x X=%02x Y=%02x S=%02x FLAGS=<N%d V%d B%d D%d I%d Z%d "
      "C%d> ticks=%u\n",
      pc, a, x, y, sp, status & FLAG_SIGN ? 1 : 0,
      status & FLAG_OVERFLOW ? 1 : 0, status & FLAG_BREAK ? 1 : 0,
      status & FLAG_DECIMAL ? 1 : 0, status & FLAG_INTERRUPT ? 1 : 0,
      status & FLAG_ZERO ? 1 : 0, status & FLAG_CARRY ? 1 : 0, ticks);
}

int main(int argc, char *argv[]) {

  FILE *fin, *fout;
  const char *romfile = NULL, *blkfile = NULL;
  int addr = -1, reset = -1, max_ticks = -1, errflg = 0, sz = 0, c;
  int io = 0xf000, brk_exit = 1;

  while ((c = getopt(argc, argv, "xr:a:g:t:m:b:")) != -1) {
    switch (c) {
    case 'x':
      brk_exit = 0;
      break;
    case 'r':
      romfile = optarg;
      break;
    case 'a':
      addr = strtol(optarg, NULL, 0);
      break;
    case 'g':
      reset = strtol(optarg, NULL, 0);
      break;
    case 't':
      max_ticks = strtol(optarg, NULL, 0);
      break;
    case 'm':
      io = strtol(optarg, NULL, 0);
      break;
    case 'b':
      blkfile = optarg;
      break;
    case ':': /* -f or -o without operand */
      fprintf(stderr, "Option -%c requires an argument\n", optopt);
      errflg++;
      break;
    case '?':
      fprintf(stderr, "Unrecognized option: '-%c'\n", optopt);
      errflg++;
    }
  }

  putc_addr = io + 1;
  getc_addr = io + 4;
  peekc_addr = io + 5;
  timer_addr = io + 6;
  blkio_addr = io + 16;

  if (romfile == NULL)
    errflg++;

  if (errflg) {
    fprintf(stderr,
            "Usage: c65 -r file.rom [...]\n"
            "Options:\n"
            "-?         : Show this message\n"
            "-r <file>  : Load file to memory and reset into it\n"
            "-a <addr>  : Address to load (default top of address space)\n"
            "-g <addr>  : Set reset vector @ 0xfffc to <address>\n"
            "-t <ticks> : Run for max ticks (default forever)\n"
            "-m <addr>  : Set magic IO base address (default 0xf000)\n"
            "-b <file>  : binary block file backing blk device\n"
            "-x         : reset via 0xfffe on BRK rather than exit\n");
    exit(2);
  }

  /* non-blocking stdin */
  //    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

  blkiop = (BLKIO *)(memory + blkio_addr);

  if (blkfile)
    fblk = fopen(blkfile, "r+b");

  fin = fopen(romfile, "rb");
  if (!fin) {
    fprintf(stderr, "File not found: %s\n", romfile);
    exit(3);
  }
  fseek(fin, 0L, SEEK_END);
  sz = ftell(fin);
  rewind(fin);
  if (addr < 0)
    addr = 65536 - sz;
  printf("c65: reading %s @ 0x%04x\n", romfile, addr);
  fread(memory + addr, 1, sz, fin);
  fclose(fin);

  if (reset >= 0) {
    memory[0xfffc] = reset & 0xff;
    memory[0xfffd] = (reset >> 8) & 0xff;
  }

  printf("c65: starting\n");
  show_cpu();
  reset6502();
  show_cpu();

  set_terminal_nb();
  while (ticks != max_ticks && !shutdown) {
    ticks += step6502();
    if (!opcode && brk_exit) shutdown = 1;
  }

  show_cpu();

  if (fblk)
    fclose(fblk);

  fout = fopen("c65-coverage.dat", "wb");
  fwrite(rws, sizeof(int), 65536, fout);
  fclose(fout);

  fout = fopen("c65-writes.dat", "wb");
  fwrite(writes, sizeof(int), 65536, fout);
  fclose(fout);
}
