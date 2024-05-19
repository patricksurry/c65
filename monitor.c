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

typedef struct Label {
    const char *label;
    uint16_t addr;
    struct Label* next;
} Label;


typedef struct Command {
    const char * name;
    const char * help;
    int repeatable;
    void (*handler)();
} Command;

Label *labels = NULL;
Command _cmds[];
int org = -1;       /* the current address, reset to PC after each simulation step */

char _prompt[512];

char* prompt() {
    sprintf(_prompt,
        "\x1b[2mPC\x1b[m %.4x  "
        "\x1b[%cmN\x1b[m\x1b[%cmV\x1b[m\x1b[2m-\x1b[m\x1b[%cmB\x1b[m\x1b[%cmD\x1b[m\x1b[%cmI\x1b[m\x1b[%cmZ\x1b[m\x1b[%cmC\x1b[m  "
        "\x1b[2mA\x1b[m %.2x \x1b[2mX\x1b[m %.2x \x1b[2mY\x1b[m %.2x \x1b[2mSP\x1b[m %.2x > ",
        pc,
        status & FLAG_SIGN ? '0' : '2', status & FLAG_OVERFLOW ? '0' : '2',
        /* - */ status & FLAG_BREAK ? '0' : '2',
        status & FLAG_DECIMAL ? '0' : '2', status & FLAG_INTERRUPT ? '0' : '2',
        status & FLAG_ZERO ? '0' : '2', status & FLAG_CARRY ? '0' : '2',
        a, x, y, sp
    );
    return _prompt;
}

void add_label(const char* label, uint16_t addr) {
    Label *l, *prv;

    /* discard any existing label(s) with matching name or address */
    for (prv=NULL, l=labels; l; prv=l, l=l->next) {
        if (l->addr == addr || 0==strcmp(l->label, label)) {
            if (prv) {
                prv->next = l->next;
            } else {
                labels = l->next;
            }
            free((void*)l->label);
            free(l);
            l = prv ? prv : labels;
        }
    }

    /* add new label to head of list*/
    l = malloc(sizeof(Label));
    l->label = strdup(label);
    l->addr = addr;
    l->next = labels;
    labels = l;
}

void del_label(uint16_t addr) {
    Label *prv, *l;

    /* find label before the one we want to delete */
    for (prv=NULL, l=labels; l && l->addr != addr; prv=l, l=l->next) /**/ ;

    if (l) {
        if (prv) {
            prv->next = l->next;
        } else {
            labels = l->next;
        }
        free((void*)l->label);
        free(l);
    }
}

const char* label_for_address(uint16_t addr) {
    Label *l;

    for (l=labels; l && l->addr != addr; l=l->next) /**/ ;
    return (l && l->addr == addr) ? l->label : NULL;
}

int address_for_label(const char* s) {
    Label *l;

    if (strcasecmp(s, "pc")) return pc;
    for (l=labels; l && strcmp(s, l->label) != 0; l=l->next) /**/ ;
    return l ? l->addr : -1;
}

/*TODO flip 0/1 for parse_xxx return with err type? */
int parse_addr(uint16_t *addr) { //}, int default) {  /*TODO -1 no default */
    /* parse address, empty defaults to PC. return 1 on success, 0 on failure */
    /*TODO explicit default */
    /*TODO check labels */
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
    /*TODO check labels */
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

char* _fmt_addr(char *buf, uint16_t addr, int len) {
    const char *label = label_for_address(addr);
    if (label) {
        sprintf(buf, "%.16s", label);
    } else {
        sprintf(buf, "$%.*x", len, addr);
    }
    return buf;
}

uint16_t disasm(uint16_t start, uint16_t end) {
    uint8_t op, k, n;
    int8_t offset;
    char line[80], buf[32], *p;
    const char *fmt, *label;
    uint16_t addr;
    /*
    0         1         2         3         4         5
    0123456789012345678901234567890123456789012345678901234567890
    ^[1m ^[m  abcd  ^[2maa bb cc  ^[mbbr3 $zp,$rr
    ^[1m*^[mB abce  ^[2maa bb     ^[mbne  $rr
    ^[1m ^[m  abdf  ^[2maa bb cc  ^[mlda  ($abcd,x)
    */
    for (addr = start; addr < end; /**/){
        label = label_for_address(addr);
        if (label) printf("%s:\n", label);
        /* clear line and show addresss with break and PC info */
        memset(line, ' ', sizeof(line));
        p = line;
        p += sprintf(line, "\x1b[1m%c\x1b[m", pc==addr?'*':' ');
        *p = ' ';
        if (breakpoints[addr] & BREAK_EXECUTE) {
            line[8] = 'B';
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
        if (n == 1) {
            p += sprintf(p, "%s", fmt);
        } else if (strchr(fmt, '#')) { /* immediate? */
            p += sprintf(p, fmt, memory[addr++]);
        } else if (strchr(fmt, ';')) { /* relative? */
            /* 1 or 2 bytes with relative address */
            offset = memory[addr + n-2];
            if (n==3) {
                p += sprintf(p, fmt, _fmt_addr(buf+16, memory[addr], 2), _fmt_addr(buf, addr+2+offset, 4), offset);
                addr+=2;
            } else {
                p += sprintf(p, fmt, _fmt_addr(buf, addr+1+offset, 4), offset);
                addr++;
            }
        } else if (n==2) {
            p += sprintf(p, fmt, _fmt_addr(buf, memory[addr], 2));
            addr++;
        } else {
            p += sprintf(p, fmt, _fmt_addr(buf, *(uint16_t*)(memory+addr), 4));
            addr += 2;
        }
        puts(line);
    }
    return addr;
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


void cmd_go() {
    /* if address ok, run indefinitely */
    /* TODO optional break addr */
    if(!parse_addr(&pc)) {
        puts("Couldn't parse address");
        return;
    }
    step_mode = STEP_RUN;
    step_target = -1;

}


void cmd_continue() {
    /* run imdefinitely */
    /*TODO optional break addr */
    step_mode = STEP_RUN;
    step_target = -1;
}

void cmd_step() {
    /* show current line and run one step */
    /*TODO optional step count */
    disasm(pc, pc+1);
    step_mode = STEP_INST;
    step_target = 1;
}

void cmd_next() {
    /*TODO optional step count*/
    disasm(pc, pc+1);
    step_mode = STEP_JSR;
    step_target = 1;
}

void cmd_call() {
    uint16_t ret = pc;
    if (!parse_addr(&pc)) {
        printf("Couldn't parse address");
    }
    breakpoints[ret] |= BREAK_ONCE;
    ret--;  // 6502 rts is weird...

    /* write return address to stack, directly via memory[] to avoid callbacks */
    memory[BASE_STACK + sp] = (ret >> 8) & 0xFF;
    memory[BASE_STACK + ((sp - 1) & 0xFF)] = ret & 0xFF;
    sp -= 2;

    step_mode = STEP_RUN;
    step_target = -1;
}


void cmd_disasm() {
    uint16_t start, end;

    if(!parse_range(&start, &end, org, 16)) {
        puts("Bad address range\n");
    } else {
        org = disasm(start, end);
    }
}


void cmd_dump() {
    uint16_t start, end;
    if (!parse_range(&start, &end, org, 64)) {
        puts("Bad address range\n");
    } else {
        dump(start, end);
        org = end;
    }
}


void cmd_stack() {
    dump(sp + 0x101, 0x200);
}


void cmd_break() {
    uint16_t start, end;
    uint8_t f=0;
    char *p;

    if (!parse_range(&start, &end, pc, 1)) {
        puts("Bad address range\n");
        return;
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
        org = start;
    } else {
        printf("Unknown breakpoint type '%c'\n", *p);
    }
}


void cmd_delete() {
    uint16_t start, end;

    if (!parse_range(&start, &end, pc, 1)) {
        puts("Bad address range\n");
    } else {
        for(/**/; start < end; start++)
            breakpoints[start] = 0;
        org = start;
    }
}


void cmd_set() {
    const char *vars = "axysp nv bdizc";    /* 0-4 are regs, 6-13 are flags*/
    uint8_t* regs[] = {&a, &x, &y, &sp}, i, v, bit;
    char *p, *q;
    p = strtok(NULL, " ");
    if (!p) {
        puts("Missing register/flag, expected one of a,x,y,sp,pc or n,v,b,d,i,z,c\n");
        return;
    }
    q = strchr(vars, tolower(*p));
    if (!q) {
        puts("Unknown register/flag, expected one of a,x,y,sp,pc or n,v,b,d,i,z,c\n");
        return;
    }
    i = q-vars;
    p = strtok(NULL, " ");
    if (!p) {
        puts("Missing value in set\n");
        return;
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
}

void cmd_store() {
    uint16_t start, end, addr;
    uint8_t val;
    char *p, *q;
    if (!parse_range(&start, &end, pc, 1)) {
        puts("Bad address range\n");
        return;
    }
    addr = start;
    org = start;
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
}

void cmd_label() {
    const char *lbl = strtok(NULL, " ");
    uint16_t addr;

    if (
        !lbl
        || isdigit(lbl[0])
        || strspn(lbl, "_@0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") != strlen(lbl)
    ) {
        puts("Invalid label\n");
        return;
    }
    if (!parse_addr(&addr)) {
        puts("Invalid address\n");
        return;
    }
    add_label(lbl, addr);
}

void cmd_unlabel() {
    /*TODO*/
}

void cmd_blockfile() {
    const char* p = strtok(NULL, " ");
    io_blkfile(p);
}

void cmd_load() {
    const char* fname = strtok(NULL, " ");
    uint16_t addr;

    if (!fname) {
        puts("Missing file name\n");
        return;
    }
    if(!parse_addr(&addr)) {
        puts("Bad or missing address\n");
        return;
    }
    (void)load_memory(fname, addr);
    org = addr;
}

void cmd_save() {
    uint16_t start, end;
    const char* fname = strtok(NULL, " ");

    if (!fname) {
        puts("Missing file name\n");
        return;
    }
    if (!parse_range(&start, &end, 0, 0)) {
        puts("Bad address range\n");
        return;
    }
    if (start == end) end = 0xffff;
    (void)save_memory(fname, start, end);
    org = start;
}


void cmd_quit() {
    monitor_exit();
    exit(0);
}


void cmd_help() {
    int i;
    for (i=0; strcmp(_cmds[i].name, "?"); i++) {
        printf("%s %s\n", _cmds[i].name, _cmds[i].help);
    }
}

/*
TODO

step / next should take optional step, print ... after first line if # > 1

label / unlabel or just clear?

stats dump command

  FILE *fout;
  fout = fopen("c65-coverage.dat", "wb");
  fwrite(rws, sizeof(int), 65536, fout);
  fclose(fout);

  fout = fopen("c65-writes.dat", "wb");
  fwrite(writes, sizeof(int), 65536, fout);
  fclose(fout);

tick limit command

  long max_ticks = -1;
    case 't':
      max_ticks = strtol(optarg, NULL, 0);
      break;
*/

Command _cmds[] = {
    { "go", "[addr] - run from address until breakpoint", 0, cmd_go },
    { "continue", "- run until breakpoint", 0, cmd_continue },
    { "step", "- [count] step by single instructions", 1, cmd_step },
    { "next", "- [count] like step but treats jsr ... rts as one step", 1, cmd_next },
    { "call", "[addr] - call subroutine leaving PC unchanged", 0, cmd_call },

    { "disassemble", "[range] - show code disassembly for range", 1, cmd_disasm },
    { "dump", "[range] - show memory contents for range", 1, cmd_dump },
    { "stack", "- show stack contents", 0, cmd_stack },
    { "break",  "[range] r|w|a|x - trigger break on read, write, any access or execute (default)", 0, cmd_break },
    { "delete",  "[range] - remove all breakpoints in range", 0, cmd_delete },
    { "set", "[a|x|y|sp|pc|n|v|d|i|z|c] value - modify a register or flag", 0, cmd_set },
    { "store", "[range] value ... - set memory contents in range to value(s)", 0, cmd_store },
    { "label", "name addr - add a symbolic name for addr", 0, cmd_label },
    { "unlabel", "name|addr - remove label by name or addr", 0, cmd_unlabel },

    { "load", "romfile [range] - read binary file to memory", 0, cmd_load },
    { "save", "romfile [range] - write memory to file", 0, cmd_save },
    { "blockfile", "[blockfile] - use binary file for block storage, empty to disable", 0, cmd_blockfile },
    { "quit", "- leave c65", 0, cmd_quit },
    { "help", "or ? - show this help", 0, cmd_help },
    { "?", "", 0, cmd_help },
};

#define n_cmds sizeof(_cmds)/sizeof(_cmds[0])

Command *_repeat_cmd = NULL;

void parse_cmd(char *line) {
    char* p = strtok(line, " ");
    Command *cmd;
    int i;

    for(i=0; i<n_cmds; i++) {
        if(!strncmp(p, _cmds[i].name, strlen(p))) {
            cmd = _cmds+i;
            _repeat_cmd = cmd->repeatable ? cmd: NULL;
            cmd->handler();
            break;
        }
    }
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

void monitor_init(const char * labelfile) {
    FILE *f;
    char *p=NULL, *s, *end;
    const char *label;
    uint16_t addr;
    size_t n=0;
    int fail=0;
    linenoiseSetCompletionCallback(completion);
    linenoiseHistorySetMaxLen(256);
    linenoiseHistoryLoad(".c65");

    if (labelfile) {
        f = fopen(labelfile, "r");
        if (!f) {
            printf("Couldn't read labels from %s\n", labelfile);
            return;
        }
        while (getline(&p, &n, f) > 0) {
            label = strtok(p, "\t =");
            s = strtok(NULL, "\t =\n");
            end = 0;
            if (s && s[0] == '$' && strlen(s) == 5) addr = strtol(s+1, &end, 16);
            if (s && end > s+1) {
                add_label(label, addr);
            } else {
                puts(p);
                fail++;
            }
        }
        free(p);
        if (fail) printf("Discarded %d lines from %s\n", fail, labelfile);
        fclose(f);
    }
}

void monitor_exit() {
    linenoiseHistorySave(".c65");
}

void monitor_command() {
    char *line;

    if (step_mode != STEP_NONE) {
        org = pc;
        step_mode = STEP_NONE;
    }

    line = linenoise(prompt());
    if(strlen(line)) {
        linenoiseHistoryAdd(line);
        parse_cmd(line);
        linenoiseFree(line);
    } else if (_repeat_cmd) {
        /* empty line can repeat some commands */
        _repeat_cmd->handler();
    }
}

