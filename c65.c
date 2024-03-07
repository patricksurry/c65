#include "fake65c02.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <readline/readline.h>

uint8_t memory[65536];
int rws[65536];
int writes[65536];

int getc_addr = 0xf004, putc_addr = 0xf001, blkio_addr = 0xf010, ticks = 0;

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
  int ch;
  if (addr == getc_addr) {
    ch = (uint8_t)getchar();
    ch = ch == 10 ? 13 : ch;
    memory[addr] = ch;
  }
  rws[addr] += 1;
  return memory[addr];
}

void write6502(uint16_t addr, uint8_t val) {
  char *line;
  int n;
  if (addr == putc_addr) {
    putc((int)val, stdout);
  } else if (addr == blkio_addr) {
    blkiop->status = 0xff;
    if (fblk) {
      if (val < 3) {
        blkiop->status = 0;
        if (val == 1 || val == 2) {
          fseek(fblk, 1024 * blkiop->blknum, SEEK_SET);
          if (val == 1)
            fread(memory + blkiop->bufptr, 1024, 1, fblk);
          else
            fwrite(memory + blkiop->bufptr, 1024, 1, fblk);
        }
      }
    }
  }
  rws[addr] += 1;
  writes[addr] += 1;
  memory[addr] = val;
}

void show_cpu() {
  printf("\nPC=%04x A=%02x X=%02x Y=%02x S=%02x FLAGS=<N%d V%d B%d D%d I%d Z%d "
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

  while ((c = getopt(argc, argv, ":r:a:g:t:i:o:x:b:")) != -1) {
    switch (c) {
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
    case 'i':
      getc_addr = strtol(optarg, NULL, 0);
      break;
    case 'o':
      putc_addr = strtol(optarg, NULL, 0);
      break;
    case 'b':
      blkfile = optarg;
      break;
    case 'x':
      blkio_addr = strtol(optarg, NULL, 0);
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
  if (romfile == NULL)
    errflg++;

  if (errflg) {
    fprintf(stderr,
            "Usage: c65 -r file.rom [...]\n"
            "Options:\n"
            "-?              : Show this message\n"
            "-r <file>       : Load file to memory and reset into it\n"
            "-a <address>    : Address to load (default top of address space)\n"
            "-g <address>    : Set reset vector @ 0xfffc to <address>\n"
            "-t <ticks>      : Run for max ticks (default forever)\n"
            "-i <address>    : magic read for getc (default 0xf004)\n"
            "-o <address>    : magic write for putc (default 0xf001)\n"
            "-x <address>    : magic blk device (default 0xf010)\n"
            "-b <file>       : binary block file backing blk device");
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
  //    while (ctx.emu.clockticks != cycles && mfio->action != 0xff)
  //    fake6502_step(&ctx);
  while (ticks != max_ticks && blkiop->action != 0xff)
    ticks += step6502();
  //    show_cpu(&ctx);
  show_cpu();

  fout = fopen("c65-coverage.dat", "wb");
  fwrite(rws, sizeof(int), 65536, fout);
  fclose(fout);

  fout = fopen("c65-writes.dat", "wb");
  fwrite(writes, sizeof(int), 65536, fout);
  fclose(fout);
}
