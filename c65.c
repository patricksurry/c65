#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>


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
int break_flag = 0, step_mode = STEP_RUN, step_target = -1;
uint16_t rw_brk;

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
    "#$%x", "%s", "%s,x", "%s,y",
    "(%s)", "(%s,x)",  "(%s),y", "%s ; %+d",
    "%s,%s  ; %+d", "%s", "%s,x", "%s,y", "(%s)", "(%s,x)"
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
  if (breakpoints[addr] & BREAK_READ) {
    break_flag |= BREAK_READ;
    rw_brk = addr;
  }
  return memory[addr];
}

void write6502(uint16_t addr, uint8_t val) {
  io_magic_write(addr, val);
  rws[addr] += 1;
  writes[addr] += 1;
  if (breakpoints[addr] & BREAK_WRITE) {
    break_flag |= BREAK_WRITE;
    rw_brk = addr;
  }
  memory[addr] = val;
}

const char *_flags = "nv bdizc";
int get_reg_or_flag(const char *name) {
    const char *q;
    /* return register or flag value with case insenstive name */
    if (0 == strcasecmp(name, "pc")) {
        return pc;
    } else if (0 == strcasecmp(name, "a")) {
        return a;
    } else if (0 == strcasecmp(name, "x")) {
        return x;
    } else if (0 == strcasecmp(name, "y")) {
        return y;
    } else if (0 == strcasecmp(name, "sp")) {
        return sp;
    } else if (strlen(name) == 1 && (q = strchr(_flags, tolower(name[0])))) {
        return status & (1 << (7-(q-_flags))) ? 1: 0;
    }
    return -1;
}

int set_reg_or_flag(const char *name, int v) {
    const char *q;
    uint8_t bit;

    /* return register or flag value with case insenstive name */
    if (0 == strcasecmp(name, "pc")) {
        pc = v;
        return 0;
    } else if (0 == strcasecmp(name, "a")) {
        a = v;
        return 0;
    } else if (0 == strcasecmp(name, "x")) {
        x = v;
        return 0;
    } else if (0 == strcasecmp(name, "y")) {
        y = v;
        return 0;
    } else if (0 == strcasecmp(name, "sp")) {
        sp = v;
        return 0;
    } else if (strlen(name) == 1 && (q = strchr(_flags, tolower(name[0])))) {
        bit = 1 << (7-(q-_flags));
        if (bit) status |= bit;
        else status ^= bit;
        return 0;
    }
    return -1;
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
  printf("c65: reading %s to $%04x:$%04x\n", romfile, addr, addr+sz-1);
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
  printf("c65: writing $%04x:$%04x to %s", start, end, romfile);
  fwrite(memory+start, 1, (end < start ? 0x10000 : end) - start + 1, fout);
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
  const char *romfile = NULL, *labelfile = NULL;
  int addr = -1, start = -1, debug = 0, errflg = 0, c;
  int brk_action = BREAK_EXIT;
  uint16_t over_addr;

  while ((c = getopt(argc, argv, "xgr:a:s:m:b:l:")) != -1) {
    switch (c) {
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

      case 'l':
        labelfile = optarg;
        /* fall through */
      case 'g':
        debug = 1;
        /* fall through */
      case 'x':
        brk_action = BREAK_BRK;
        break;

      case ':': /* option without operand */
        fprintf(stderr, "Option -%c requires an argument\n", optopt);
        errflg++;
        break;

      case '?':
        fprintf(stderr, "Unrecognized option: '-%c'\n", optopt);
        errflg++;
        break;
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
            "-l <file>  : Read labels from file (implies -g)\n"
            "-x         : BRK should reset via $fffe rather than exit (implied by -g)\n"
            "-g         : Run with interactive debugger\n"
            "Note: address arguments can be specified in hex like 0x1234\n");
    exit(2);
  }

  if (load_memory(romfile, addr) != 0) exit(3);

  reset6502();
  if (start >= 0)
    pc = (uint16_t)start;
  show_cpu();

  io_init(debug);
  if (debug) monitor_init(labelfile);

  /*
  The simulator runs in one of several states:

  STEP_NONE - simulation paused, e.g. during monitor command sequences.
  STEP_INST - execute one instruction, counting a step towards step_target.
  STEP_NEXT - execute any instruction except JSR, counting a step.
    for JSR set over_addr to pc+3 and switch to STEP_OVER mode
  STEP_OVER - execute instructions until pc = over_addr,
    then count one step and switch back to STEP_NEXT
  STEP_RUN - execute instructions until BREAK condition

  Any BRK opcode generates BREAK_BRK or BREAK_EXIT (see -x)
  Ctrl-C (SIGINT) generates BREAK_INT
  */

  while (!(break_flag & BREAK_EXIT)) {
    if (debug) {
      do { monitor_command(); } while (step_mode == STEP_NONE);
    }
    break_flag = 0;
    while (!break_flag && (step_mode == STEP_RUN || step_target)) {
      if (step_mode == STEP_NEXT && memory[pc] == 0x20) { /* JSR ? */
        step_mode = STEP_OVER;
        over_addr = pc+3;
      }
      ticks += step6502();
      if (step_mode == STEP_OVER && pc == over_addr) step_mode = STEP_NEXT;
      if (opcode == 0x00) break_flag |= brk_action;  /* BRK ? */
      if (breakpoints[pc] & BREAK_PC) break_flag |= BREAK_PC;
      if (breakpoints[pc] & BREAK_ONCE) {
        break_flag |= BREAK_ONCE;
        breakpoints[pc] ^= BREAK_ONCE;
      }
      if (step_mode == STEP_NEXT || step_mode == STEP_INST) step_target--;
    }
  }
  show_cpu();
  io_exit();
  if (debug) monitor_exit();
}
