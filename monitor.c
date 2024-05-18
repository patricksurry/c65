#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>

#define FAKE6502_NOT_STATIC 1
#define FAKE6502_INCLUDE 1
#include "fake65c02.h"

#include "monitor.h"
#include "c65.h"
#include "magicio.h"
#include "linenoise.h"


typedef struct Command {
    const char * name;
    const char * help;
    int (*handler)();
} Command;

Command _cmds[];


const char *prompt_fmt = "\x1b[2mPC\x1b[m %.4x  \x1b[%cmN\x1b[m\x1b[%cmV\x1b[m\x1b[2m-\x1b[m\x1b[%cmB\x1b[m\x1b[%cmD\x1b[m\x1b[%cmI\x1b[m\x1b[%cmZ\x1b[m\x1b[%cmC\x1b[m  \x1b[2mA\x1b[m %.2x \x1b[2mX\x1b[m %.2x \x1b[2mY\x1b[m %.2x \x1b[2mSP\x1b[m %.2x > ";
char _prompt[512];

char* prompt() {
    sprintf(_prompt, prompt_fmt,
        pc,
        status & FLAG_SIGN ? '0' : '2', status & FLAG_OVERFLOW ? '0' : '2',
        /* - */ status & FLAG_BREAK ? '0' : '2',
        status & FLAG_DECIMAL ? '0' : '2', status & FLAG_INTERRUPT ? '0' : '2',
        status & FLAG_ZERO ? '0' : '2', status & FLAG_CARRY ? '0' : '2',
        a, x, y, sp
    );
    return _prompt;
}


int parse_addr(uint16_t *addr) {
    /* parse address, empty defaults to PC. return 1 on success, 0 on failure */
    char *p, *q;
    int v;
    p = strtok(NULL, " ");
    if (!p) {
        *addr = pc;
        return 1;
    }
    v = strtol(p, &q, 16);
    if (q!=p) *addr = (uint16_t)v;
    return q==p ? 0: 1;
}


int parse_range(uint16_t* start, uint16_t* end, uint16_t default_start, uint16_t default_length) {
    /*TODO return 0 on error, 1 on success */
    /* /35 1234/35 or 1234-5678 or 1234 */
    char *p, *q;
    uint16_t v;

    p = strtok(NULL, " ");
    q = p;
    v = p ? strtol(p, &q, 16): 0;
    *start = q==p ? default_start : v;
    switch (q?*q:0) {
        case '/':
            v = strtol(q+1, NULL, 16);
            *end = *start + (v ? v : default_length);
            break;
        case ':':
            v = strtol(q+1, NULL, 16);
            *end = v ? v : (*start + default_length);
            break;
        default:
            *end = *start + default_length;
    }
    return 1;
}


void disasm(uint16_t start, uint16_t end) {
    uint8_t op, k, n;
    int8_t offset;
    char line[64], *p;
    const char *fmt;
    uint16_t addr;
    /*
    0         1         2         3         4         5
    0123456789012345678901234567890123456789012345678901234567890
    ^[1m ^[m  abcd  ^[2maa bb cc  ^[mbbr3 $zp,$rr
    ^[1m*^[mB abce  ^[2maa bb     ^[mbne  $rr
    ^[1m ^[m  abdf  ^[2maa bb cc  ^[mlda  ($abcd,x)
    */
    line[63]=0;
    for (addr = start; addr < end; /**/){
        /* clear line and show addresss with break and PC info */
        memset(line, ' ', 63);
        p = line;
        p += sprintf(line, "\x1b[1m%c\x1b[m", pc==addr?'*':' ');
        *p = ' ';
        if (breakpoints[addr] & BREAK_EXECUTE) {
            line[8] = breakpoints[addr] & BREAK_ONCE ? 'b': 'B';
        }
        p = line+10;
        p += sprintf(p, "%.4x  \x1b[2m", addr);

        /* show bytes associated with this opcode */
        op = memory[addr];
        n = oplen(op);
        for (k=0; k<n; k++)
            p += sprintf(p, "%.2x ", memory[addr+k]);
        *p = ' ';

        /* show the name of the opcode */
        p = line+30;
        p += sprintf(p, "\x1b[m%.4s ", opname(op));
        addr++;

        /* show the addressing mode detail */
        fmt = opfmt(op);
        if (strchr(fmt, ';')) { /* relative? */
            /* 1 or 2 bytes with relative address */
            offset = memory[addr + n-2];
            if (n==3) {
                p += sprintf(p, fmt, memory[addr], addr+offset, offset);
                addr++;
            } else {
                p += sprintf(p, fmt, addr+offset, offset);
            }
            addr++;
        } else if (n == 1) {
            p += sprintf(p, "%s", fmt);
        } else if (n==2) {
            p += sprintf(p, fmt, memory[addr++]);
        } else {
            p += sprintf(p, fmt, *(uint16_t*)(memory+addr));
            addr += 2;
        }
        *p = ' ';
        printf("%s\n", line);
    }

}


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
            c = breakpoints[addr];
            c = c & BREAK_READ ? (c & BREAK_WRITE ? '*': '@') : (c & BREAK_WRITE ? '!': ' ');
            p += sprintf(p, "%.2x%c", memory[addr++], c);
            *p = ' ';
            nibble = addr & 0xf;
            if (nibble == 8) p++;
        } while(nibble && addr < end);

        printf("%s\n", line);
    }
}


int cmd_go() {
    /* if address ok, run indefinitely */
    if(!parse_addr(&pc)) {
        puts("Couldn't parse address");
        return 0;
    }
    return -1;
}


int cmd_continue() {
    /* run imdefinitely */
    return -1;
}

int cmd_step() {
    /* show current line and run one step */
    disasm(pc, pc+1);
    return 1;
}

int cmd_next() {
    if (memory[pc] == 0x20) {
        breakpoints[pc+3] |= BREAK_ONCE;
        return -1;
    } else {
        return 1;
    }
}

int cmd_call() {
    uint16_t ret = pc;
    if (!parse_addr(&pc)) {
        printf("Couldn't parse address");
        return 0;
    }
    breakpoints[ret] |= BREAK_ONCE;
    ret--;  // 6502 rts is weird...

    /* write return address to stack, directly via memory[] to avoid callbacks */
    memory[BASE_STACK + sp] = (ret >> 8) & 0xFF;
    memory[BASE_STACK + ((sp - 1) & 0xFF)] = ret & 0xFF;
    sp -= 2;

    return -1;
}


int cmd_disasm() {
    uint16_t start, end;

    if(!parse_range(&start, &end, pc, 16)) {
        puts("Bad address range\n");
    } else {
        disasm(start, end);
    }
    return 0;
}


int cmd_dump() {
    uint16_t start, end;
    if (!parse_range(&start, &end, pc, 64)) {
        puts("Bad address range\n");
    } else {
        dump(start, end);
    }
    return 0;
}


int cmd_break() {
    uint16_t start, end;
    uint8_t f=0;
    char *p;

    if (!parse_range(&start, &end, pc, 1)) {
        puts("Bad address range\n");
        return 0;
    }
    p = strtok(NULL, " ");
    switch(p?tolower(*p):'x') {
        case 'r':
            f = BREAK_READ;
            break;
        case 'w':
            f = BREAK_WRITE;
            break;
        case 'a':
            f = BREAK_ACCESS;
            break;
        case 'x':
            f = BREAK_ALWAYS;
            break;
    }
    if (f) {
        for(/**/; start < end; start++)
            breakpoints[start] |= f;
    } else {
        printf("Unknown breakpoint type '%c'\n", *p);
    }
    return 0;
}


int cmd_delete() {
    uint16_t start, end;

    if (!parse_range(&start, &end, pc, 1)) {
        puts("Bad address range\n");
    } else {
        for(/**/; start < end; start++)
            breakpoints[start] = 0;
    }
    return 0;
}


int cmd_set() {
    const char *vars = "axysp nv bdizc";    /* 0-4 are regs, 6-13 are flags*/
    uint8_t* regs[] = {&a, &x, &y, &sp}, i, v, bit;
    char *p, *q;
    p = strtok(NULL, " ");
    if (!p)
        puts("Missing register/flag, expected one of a,x,y,sp,pc or n,v,b,d,i,z,c\n");
    q = strchr(vars, tolower(*p));
    if (!q)
        puts("Unknown register/flag, expected one of a,x,y,sp,pc or n,v,b,d,i,z,c\n");
    i = q-vars;
    p = strtok(NULL, " ");
    if (!p) {
        puts("Missing value in set\n");
        return 0;
    }
    v = strtol(p, NULL, 16);

    if (i < 4) {
        *(regs[i]) = v;
    } else if (i==4) { /* PC is not a uint8_t */
        pc = v;
    } else {
        bit = 1 << (13-i);
        if (v) {
            status |= bit;
        } else {
            status &= 255 - bit;
        }
    }
    return 0;
}

int cmd_store() {
    uint16_t start, end, addr;
    uint8_t val;
    char *p, *q;
    if (!parse_range(&start, &end, pc, 1)) {
        puts("Bad address range\n");
        return 0;
    }
    addr = start;
    while((p = strtok(NULL, " "))) {
        val = strtol(p, &q, 16);
        if (q==p) {
            printf("Failed to parse value '%s'\n", p);
            break;
        }
        memory[addr++] = val;
    }
    if (addr == start) { /* default to zero fill */
        memory[addr++] = 0;
    }
    while (addr < end) {
        memory[addr++] = memory[start++];
    }
    return 0;
}

int cmd_blockfile() {
    const char* p = strtok(NULL, " ");
    io_blkfile(p);
    return 0;
}

int cmd_load() {
    const char* fname = strtok(NULL, " ");
    uint16_t addr;

    if (!fname) {
        puts("Missing file name\n");
        return 0;
    }
    if(!parse_addr(&addr)) {
        puts("Bad or missing address\n");
        return 0;
    }
    (void)load_memory(fname, addr);
    return 0;
}

int cmd_save() {
    uint16_t start, end;
    const char* fname = strtok(NULL, " ");

    if (!fname) {
        puts("Missing file name\n");
        return 0;
    }
    if (!parse_range(&start, &end, 0, 0)) {
        puts("Bad address range\n");
        return 0;
    }
    if (start == end) end = 0xffff;
    (void)save_memory(fname, start, end);
    return 0;
}

int cmd_quit() {
    monitor_exit();
    exit(0);
}


int cmd_help() {
    int i;
    for (i=0; strcmp(_cmds[i].name, "?"); i++) {
        printf("%s %s\n", _cmds[i].name, _cmds[i].help);
    }
    return 0;
}

/*TODO

step / next should take optional step, print ... after first line if # > 1
curr_addr reset if return steps != 0 default to PC
repeat dump, disasm after advancing curr addr
add .repeat to cmd struct

label / unlabel or just clear?

stats dump command
*/
/*
  FILE *fout;
  fout = fopen("c65-coverage.dat", "wb");
  fwrite(rws, sizeof(int), 65536, fout);
  fclose(fout);

  fout = fopen("c65-writes.dat", "wb");
  fwrite(writes, sizeof(int), 65536, fout);
  fclose(fout);
*/
/* tick limit command */
/*
long max_ticks = -1;
    case 't':
      max_ticks = strtol(optarg, NULL, 0);
      break;
*/

Command _cmds[] = {
    { "go", "[addr] - run from address until breakpoint", cmd_go },
    { "continue", "- run until breakpoint", cmd_continue },
    { "step", "- execute next instruction, descend into subroutine", cmd_step },
    { "next", "- execute next instruction, or entire subroutine", cmd_next },
    { "call", "[addr] - call subroutine leaving PC unchanged", cmd_call },

    { "disassemble", "[range] - show code disassembly for range", cmd_disasm },
    { "dump", "[range] - show memory contents for range", cmd_dump },
    { "break",  "[range] r|w|a|x - trigger break on read, write, any access or execute (default)", cmd_break },
    { "delete",  "[range] - remove all breakpoints in range", cmd_delete },
    { "set", "[a|x|y|sp|pc|n|v|d|i|z|c] value - modify a register or flag", cmd_set },
    { "store", "[range] value ... - set memory contents in range to value(s)", cmd_store },

    { "load", "romfile [range] - read binary file to memory", cmd_load },
    { "save", "romfile [range] - write memory to file", cmd_save },
    { "blockfile", "[blockfile] - use binary file for block storage, empty to disable", cmd_blockfile },
    { "quit", "- leave c65", cmd_quit },
    { "help", "or ? - show this help", cmd_help },
    { "?", "", cmd_help },
};

#define n_cmds sizeof(_cmds)/sizeof(_cmds[0])

Command *_repeat_cmd = NULL;

int parse_cmd(char *line) {
    char* p = strtok(line, " ");
    int i;
    int (*f)();

    for(i=0; i<n_cmds; i++) {
        if(!strncmp(p, _cmds[i].name, strlen(p))) {
            f = _cmds[i].handler;
            _repeat_cmd = (f == cmd_step || f == cmd_next) ? _cmds+i: NULL;
            return f();
        }
    }
    return 0;
}

void completion(const char *buf, linenoiseCompletions *lc) {
    int i;
    if (!strlen(buf)) return;
    for(i=0; i<n_cmds; i++) {
        if(!strncmp(buf, _cmds[i].name, strlen(buf))) {
            linenoiseAddCompletion(lc, _cmds[i].name);
        }
    }
}

void monitor_init() {
    linenoiseSetCompletionCallback(completion);
    linenoiseHistorySetMaxLen(256);
    linenoiseHistoryLoad(".c65");
}

void monitor_exit() {
    linenoiseHistorySave(".c65");
}

int monitor_command() {
    char *line;
    int steps = 0;
    line = linenoise(prompt());
    if(strlen(line)) {
        linenoiseHistoryAdd(line);
        steps = parse_cmd(line);
        linenoiseFree(line);
    } else if (_repeat_cmd) {
        /*TODO default repeat some cmds like step after empty line*/
        steps = _repeat_cmd->handler();
    }
    return steps;
}

