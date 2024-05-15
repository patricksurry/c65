#include "fake65c02.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>


#define BREAK_ALWAYS 1
#define BREAK_ONCE  2
#define BREAK_READ  4
#define BREAK_WRITE 8

uint8_t memory[65536];
uint8_t breakpoints[65536];

uint8_t read6502(uint16_t addr) { return 0; }
void write6502(uint16_t addr, uint8_t val) {}


void dump(uint16_t start, uint16_t end) {
    /*
    0         1         2         3         4         5         6         7
    01234567890123456789012345678901234567890123456789012345678901234567890123
    0000  d8 a9 78 85 12 a9 be 85  13 a2 1d bd 49 a3 95 00  |..x.........I...|
    */
    uint16_t addr = start, nibble = addr & 0xf;
    char line[75], *p;
    uint8_t c;

    line[74] = 0;

    while(addr < end) {
        memset(line, ' ', 74);
        line[56] = '|';
        line[73] = '|';

        p = line;
        p += sprintf(p, "%.4x", addr & 0xfff0);
        *p = ' ';
        p += 2 + nibble*3 + (nibble > 7 ? 1: 0);

        do {
            c = memory[addr];
            line[57 + nibble] = (c >= 32 && c < 128 ? c : '.');
            p += sprintf(p, "%.2x ", memory[addr++]);
            *p = ' ';
            nibble = addr & 0xf;
            if (nibble == 8) p++;
        } while(nibble);

        printf("%s\n", line);
    }
}

void (*mode_fns[])() = {
    /* 0-1: 0 bytes */
    imp, acc,
    /* 2-9: 1 byte */
    imm, zp, zpx, zpy,
    ind0, indx, indy, rel,      /* mode 9 and 10 include a relative address byte */
    /* 10+: 2 bytes */
    zprel, abso, absx, absy, ind, ainx
};

const uint8_t n_modes = sizeof(mode_fns)/sizeof(mode_fns[0]);

const char * mode_fmts[] = {
    "", "a",
    "#$%x", "$%.2x", "$%.2x,x", "$%.2x,y",
    "($%.2x)", "($%.2x,x)",  "($%.2x),y", "%c$%x",
    "$%.2x,%c$%x", "$%.4x", "$%.4x,x", "$%.4x,y", "($%.4x)", "($%.4x,x)"
};

uint8_t mode_index(uint8_t opcode) {
    uint8_t k;
    for (k=0; k<n_modes && mode_fns[k] != addrtable[opcode]; k++) /**/ ;
    assert(k < n_modes);
    return k;
}

uint8_t op_bytes(uint8_t opcode) {
    uint8_t k = mode_index(opcode);
    return k < 2 ? 1 : ( k < 10 ? 2: 3);
}

void disasm(uint16_t start, uint16_t end) {
    uint8_t op, k;
    int8_t offset;
    void *mode;
    char line[48], *p;
    const char *fmt;
    uint16_t addr;
    /*
    0         1         2         3         4
    0123456789012345678901234567890123456789012345678
    >BWR  abcd  aa bb cc  bbr3 $zp,$rr    ^ $abcd
          abce  aa bb     bne  $rr        v $abcd
          abdf  aa bb cc  lda  ($abcd,x)
    */
    line[47]=0;
    for (addr = start; addr < end; /**/){
        /* clear line and show addresss with break and PC info */
        memset(line, ' ', 47);
        line[0] = pc == addr ? '>': ' ';
        k = breakpoints[addr];
        if (k) {
            line[2] = k & BREAK_ALWAYS ? 'B' : (k & BREAK_ONCE ? '1': ' ');
            line[3] = k & BREAK_READ ? 'R': ' ';
            line[4] = k & BREAK_WRITE ? 'W': ' ';
        }
        p = line+6;
        p += sprintf(p, "%.4x  ", addr);

        /* show bytes associated with this opcode */
        op = memory[addr];
        for (k=0; k<op_bytes(op); k++)
            p += sprintf(p, "%.2x ", memory[addr+k]);
        *p = ' ';

        /* show the name of the opcode */
        p = line+22;
        p += sprintf(p, "%.4s", opnametable + op*4);
        addr++;

        /* show the addressing mode detail */
        k = mode_index(op);
        fmt = mode_fmts[k];
        if (k < 2) {
            /* 0 byte */
            p += sprintf(p, "%s", fmt);
        } else if (k < 9) {
            /* 1 byte, not relative */
            p += sprintf(p, fmt, memory[addr++]);
        } else if (k > 10) {
            /* 2 byte, not relative */
            p += sprintf(p, fmt, *(uint16_t*)(memory+addr));
            addr += 2;
        } else {
            /* 1 or 2 bytes with relative address */
            offset = memory[addr + ((k == 10) ? 1: 0)];
            if (k == 10) {
                p += sprintf(p, fmt, memory[addr], offset < 0 ? '-': '+', (uint8_t)abs(offset));
                addr++;
            } else {
                p += sprintf(p, fmt, offset < 0 ? '-': '+', (uint8_t)abs(offset));
            }
            addr++;
            /* show the target address */
            sprintf(line+38, "%c $%.4x", offset < 0 ? '^':'v', addr+offset);
        }
        *p = ' ';
        printf("%s\n", line);
    }
}


int main(void) {
    FILE *fin;

    fin = fopen("bboard.rom", "rb");
    fread(memory+32768, 1, 32768, fin);
    fclose(fin);

    dump(32768, 32768+512);
    pc = 0x8011;
    breakpoints[0x800b] = BREAK_ONCE | BREAK_WRITE;
    disasm(32768, 32768+64);
}
