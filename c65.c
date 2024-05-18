#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#define FAKE6502_NOT_STATIC 1
#include "fake65c02.h"
#include "c65.h"
#include "magicio.h"
#include "monitor.h"


uint8_t memory[65536];
uint8_t breakpoints[65536];
int rws[65536];
int writes[65536];

long ticks = 0;
int break_flag = 0;


static uint8_t _opmodes[256] = { 255 };

static void (*_addrmodes[])() = {
    /* 0-1: 0 bytes */
    imp, acc,
    /* 2-9: 1 byte */
    imm, zp, zpx, zpy,
    ind0, indx, indy, rel,      /* mode 9 and 10 include a relative address byte */
    /* 10+: 2 bytes */
    zprel, abso, absx, absy, ind, ainx
};

#define n_addrmodes sizeof(_addrmodes)/sizeof(_addrmodes[0])

static const char* _opfmts[] = {
    "", "a",
    "#$%x", "$%.2x", "$%.2x,x", "$%.2x,y",
    "($%.2x)", "($%.2x,x)",  "($%.2x),y", "$%.4x ; %+d",
    "$%.2x,$%.4x ; %+d", "$%.4x", "$%.4x,x", "$%.4x,y", "($%.4x)", "($%.4x,x)"
};


const char* opname(uint8_t op) {
    return opnames + op*4;
}


uint8_t opmode(uint8_t op) {
    int i, k;
    if (_opmodes[0] == 255) {
        /* initialize _opmodes from addrtable */
        for(i=0; i<256; i++) {
            for (k=0; k<n_addrmodes && _addrmodes[k] != addrtable[i]; k++) /**/ ;
            _opmodes[i] = k;
        }
    }
    return _opmodes[op];
}

uint8_t oplen(uint8_t op) {
    uint8_t k = opmode(op);
    return k < 2 ? 1 : ( k < 10 ? 2: 3);
}


const char* opfmt(uint8_t op) {
    return _opfmts[opmode(op)];
}


uint8_t read6502(uint16_t addr) {
  io_magic_read(addr);
  rws[addr] += 1;
  break_flag |= (breakpoints[addr] & BREAK_READ);
  return memory[addr];
}


void write6502(uint16_t addr, uint8_t val) {
  io_magic_write(addr, val);
  rws[addr] += 1;
  writes[addr] += 1;
  break_flag |= breakpoints[addr] & BREAK_WRITE;
  memory[addr] = val;
}


int load_memory(const char* romfile, int addr) {
  /*
    read ROM @ addr, return 0 on success
    if addr < 0, align to top of memory
    */
  FILE *fin;
  int sz;

  fin = fopen(romfile, "rb");
  if (!fin) {
    fprintf(stderr, "File not found: %s\n", romfile);
    return -1;
  }
  fseek(fin, 0L, SEEK_END);
  sz = ftell(fin);
  rewind(fin);
  if (addr < 0)
    addr = 65536 - sz;
  printf("Reading %s to $%04x:$%04x\n", romfile, addr, addr+sz-1);
  fread(memory + addr, 1, sz, fin);
  fclose(fin);
  return 0;
}

int save_memory(const char* romfile, uint16_t start, uint16_t end) {
  FILE *fout;
  fout = fopen(romfile, "wb");
  if (!fout) {
    fprintf(stderr, "File not found: %s\n", romfile);
    return -1;
  }
  printf("Writing $%04x:$%04x to %s", start, end, romfile);
  fwrite(memory+start, 1, end-start+1, fout);
  fclose(fout);
  return 0;
}

void show_cpu() {
  printf(
      "c65: PC=%04x A=%02x X=%02x Y=%02x S=%02x FLAGS=<N%d V%d B%d D%d I%d Z%d "
      "C%d> ticks=%lu\n",
      pc, a, x, y, sp, status & FLAG_SIGN ? 1 : 0,
      status & FLAG_OVERFLOW ? 1 : 0, status & FLAG_BREAK ? 1 : 0,
      status & FLAG_DECIMAL ? 1 : 0, status & FLAG_INTERRUPT ? 1 : 0,
      status & FLAG_ZERO ? 1 : 0, status & FLAG_CARRY ? 1 : 0, ticks);
}

int main(int argc, char *argv[]) {
  const char *romfile = NULL;
  int addr = -1, start = -1, debug = 0, errflg = 0, c;
  int brk_action = BREAK_SHUTDOWN;

  while ((c = getopt(argc, argv, "xgr:a:s:m:b:")) != -1) {
    switch (c) {
    case 'x':
      brk_action = 0;
      break;
    case 'g':
      debug = 1;
      break;
    case 'r':
      romfile = optarg;
      break;
    case 'a':
      addr = strtol(optarg, NULL, 0);
      break;
    case 's':
      start = strtol(optarg, NULL, 0);
      break;
    case 'm':
      io_addr = strtol(optarg, NULL, 0);
      break;
    case 'b':
      io_blkfile(optarg);
      break;
    case ':': /* option without operand */
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
            "-?         : Show this message\n"
            "-r <file>  : Load file and reset into it via address at fffc\n"
            "-a <addr>  : Load at address instead of aligning to end of memory\n"
            "-s <addr>  : Start executing at addr instead of via reset vector\n"
            "-m <addr>  : Set magic IO base address (default 0xf000)\n"
            "-b <file>  : Use binary file for magic block storage\n"
            "-x         : BRK should reset via $fffe rather than exit\n"
            "-g         : Run with interactive debugger\n"
            "Note: write hex address arguments like 0x1234\n");
    exit(2);
  }

  if (load_memory(romfile, addr) != 0) exit(3);

  io_init(debug);
  if (debug) monitor_init();

  printf("c65: starting\n");
  show_cpu();
  reset6502();
  show_cpu();

  if (start >= 0)
    pc = (uint16_t)start;

  int steps;
  while (!(break_flag & BREAK_SHUTDOWN)) {
    steps = debug ? monitor_command() : -1;
    break_flag = 0;
    while (steps && !break_flag) {
      ticks += step6502();
      break_flag |= breakpoints[pc] & BREAK_EXECUTE;
      if (opcode == 0) break_flag |= BREAK_ONCE | brk_action;
      if (steps > 0) steps--;
    }
  }
  show_cpu();
  io_exit();
  if (debug) monitor_exit();
}
