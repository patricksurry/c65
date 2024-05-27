#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>

#define FAKE6502_NOT_STATIC 1
#define FAKE6502_INCLUDE 1
#include "fake65c02.h"

#include "monitor.h"
#include "parse.h"
#include "c65.h"
#include "magicio.h"
#include "linenoise.h"


typedef struct Command {
    const char *name;
    const char *help;
    int repeatable;
    void (*handler)();
} Command;


Command _cmds[];
int org = -1;       /* the current address, reset to PC after each simulation step */

char _prompt[512];

#define TXT_LO "\x1b[34m"
#define TXT_B1 "\x1b[41m"
#define TXT_B2 "\x1b[42m"
#define TXT_B3 "\x1b[44m"
#define TXT_N  "\x1b[0m"

#define FLAG_FMT(f, ch) (status & FLAG_##f ? TXT_B3 : TXT_LO), (status & FLAG_##f ? ch: tolower(ch))

char* prompt() {
    sprintf(_prompt,
        TXT_LO "PC" TXT_N " %.4x  "
        "%s%c" TXT_N "%s%c" TXT_N TXT_LO "-" TXT_N "%s%c" TXT_N
        "%s%c" TXT_N "%s%c" TXT_N "%s%c" TXT_N "%s%c" TXT_N "  "
        TXT_LO "A " TXT_N "%.2x " TXT_LO "X " TXT_N "%.2x " TXT_LO "Y " TXT_N "%.2x " TXT_LO "SP " TXT_N "%.2x "
        TXT_LO "> " TXT_N,
        pc,
        FLAG_FMT(SIGN, 'N'), FLAG_FMT(OVERFLOW, 'V'), /* - */ FLAG_FMT(BREAK, 'B'),
        FLAG_FMT(DECIMAL, 'D'), FLAG_FMT(INTERRUPT, 'I'), FLAG_FMT(ZERO, 'Z'), FLAG_FMT(CARRY, 'C'),
        a, x, y, sp
    );
    return _prompt;
}

char* _fmt_addr(char *buf, uint16_t addr, int len) {
    const Symbol *sym = get_next_symbol_by_value(NULL, addr);
    if (sym) {
        sprintf(buf, "%.16s", sym->name);
    } else {
        sprintf(buf, "$%.*x", len, addr);
    }
    return buf;
}

uint16_t disasm(uint16_t start, uint16_t end) {
    uint8_t op, k, n;
    int8_t offset;
    char line[80], buf[32], *p;
    const char *fmt;
    const Symbol *sym;
    const int n_fmt = strlen(TXT_LO);
    int addr, endl = end < start ? 0x10000 : end;

    /*
    0          1          2         3
    012345678 9012345678 90123456789012345
       abcd  <aa bb cc  >bbr3 $zp,$rr
    *B abce  <aa bb     >bne  $rr
       abdf  <aa bb cc  >lda  ($abcd,x)
    */
    for (addr = start; addr < endl; /**/){
        /* show any labels for this address */
        sym = NULL;
        for(;;) {
            sym = get_next_symbol_by_value(sym, addr);
            if (!sym) break;
            printf("%s:\n", sym->name);
        }

        /* clear line and show addresss with break and PC info */
        memset(line, ' ', sizeof(line));
        p = line;
        p += sprintf(
            line, "%c%c %.4x  " TXT_LO,
            pc == addr ? '*' : ' ',
            breakpoints[addr] & BREAK_PC ? 'B': ' ',
            addr
        );

        /* show bytes associated with this opcode */
        op = memory[addr];
        n = oplen(op);
        for (k=0; k<n; k++)
            p += sprintf(p, "%.2x ", memory[addr+k]);
        *p = ' ';

        /* show the name of the opcode */
        p = line + 19 + n_fmt;
        p += sprintf(p, TXT_N "%.4s ", opname(op));
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
    int addr = start & 0xfff0, endl = end < start ? 0x10000 : end;
    char line[256], chrs[16], *p;
    uint8_t c, v;

    puts("       0  1  2  3  4  5  6  7   8  9  a  b  c  d  e  f   0123456789abcdef");
    while(addr < endl) {
        memset(chrs, ' ', 16);
        p = line;
        /* show the address */
        p += sprintf(p, "%.4x  ", addr);
        /* show blanks before truncated address */
        while (addr < start) p += sprintf(p, "   %s", (++addr & 0xf) == 8 ? " ":"");

        do {
            if (addr < endl) {
                c = memory[addr];
                chrs[addr & 0xf] = (c >= 32 && c < 128 ? c : '.');
                c = breakpoints[addr] & (BREAK_READ | BREAK_WRITE);
                v = memory[addr++];
                p += sprintf(p, "%s%.2x%s%s",
                    c & BREAK_READ ?
                        (c & BREAK_WRITE ? TXT_B1: TXT_B2)
                        : (c & BREAK_WRITE ? TXT_B3: ""),
                    v,
                    c ? TXT_N: "",
                    (addr & 0xf) == 8 ? "  " : " "
                );
            } else {
                addr++;
                p += sprintf(p, "   %s", (addr & 0xf) == 8 ? " ":"");
            }
        } while((addr & 0xf) && addr < endl);
        /* show blanks padding end of range */
        while (addr & 0xf) p += sprintf(p, "   %s", (++addr & 0xf) == 8 ? " ":"");

        sprintf(p, " |%.16s|", chrs);
        puts(line);
    }
}


void cmd_go() {
    /* run indefinitely from optional addr or PC*/
    if (E_OK != parse_addr(&pc, pc) || E_OK != parse_end()) return;

    step_mode = STEP_RUN;
}


void cmd_continue() {
    /* run imdefinitely, optionally to one-time breakpoint */
    uint16_t addr;
    if (E_OK != parse_addr(&addr, pc) || E_OK != parse_end()) return;

    if (addr != pc) breakpoints[addr] |= BREAK_ONCE;
    step_mode = STEP_RUN;
}

void _cmd_single(int mode) {
    int v;

    step_mode = mode;
    if (E_OK != parse_int(&v, 1) || E_OK != parse_end()) return;
    step_target = v;
    if (step_target > 1) puts("...");
}

void cmd_step() {
    /* show current line and run one step */
    _cmd_single(STEP_INST);
}

void cmd_next() {
    /* like step, but treat jsr ... rts as one step */
    _cmd_single(STEP_NEXT);
}

void cmd_call() {
    uint16_t ret = pc, target;

    if (E_OK != parse_addr(&target, DEFAULT_REQUIRED) || E_OK != parse_end()) return;

    /* trigger break once we return from subroutine */
    pc = target;
    breakpoints[ret] |= BREAK_ONCE;
    ret--;  // 6502 rts is weird...

    /* write return address to stack, directly via memory[] to avoid callbacks */
    memory[BASE_STACK + sp] = (ret >> 8) & 0xFF;
    memory[BASE_STACK + ((sp - 1) & 0xFF)] = ret & 0xFF;
    sp -= 2;

    step_mode = STEP_RUN;
}

void cmd_disasm() {
    uint16_t start, end;

    if (E_OK != parse_range(&start, &end, org, 16) || E_OK != parse_end()) return;

    org = disasm(start, end);
}

void cmd_memory() {
    uint16_t start, end;

    if (E_OK != parse_range(&start, &end, org, 64) || E_OK != parse_end()) return;

    dump(start, end);
    org = end;
}

void cmd_stack() {
    if (E_OK != parse_end()) return;

    dump(sp + 0x101, 0x200);
}

void cmd_break() {
    uint16_t start, end;
    uint8_t f=0;
    int endl, addr;
    char *p;

    if (E_OK != parse_range(&start, &end, pc, 1)) return;
    endl = end < start ? 0x10000 : end;

    p = parse_delim();
    if (E_OK != parse_end()) return;
    switch(p ? tolower(*p):'x') {
        case 'r':
            f = BREAK_READ;
            break;
        case 'w':
            f = BREAK_WRITE;
            break;
        case 'a':
            f = BREAK_READ | BREAK_WRITE;
            break;
        case 'x':
            f = BREAK_PC;
            break;
    }
    if (f) {
        for(addr=start; addr < endl; addr++)
            breakpoints[addr] |= f;
        org = start;
    } else {
        printf("Unknown breakpoint type '%c'\n", *p);
    }
}

void cmd_delete() {
    uint16_t start, end;
    int addr, endl, n;

    if (E_OK != parse_range(&start, &end, pc, 1) || E_OK != parse_end()) return;

    endl = end <= start ? 0x10000 : end;

    for(n=0, addr=start; addr < endl; addr++)
        if (breakpoints[addr]) {
            breakpoints[addr] = 0;
            n++;
        }
    org = start;
    printf("Removed %d breakpoint%s.\n", n, n==1?"":"s");
}

void cmd_show() {
    uint16_t start, end;
    int endl, addr;
    const Symbol *p;

    if (E_OK != parse_range(&start, &end, 0, 0) || E_OK != parse_end()) return;

    printf("show %d %d\n", start, end);
    endl = end <= start ? 0x10000 : end;
    puts("E(x)ecution breakpoints:");
    for (addr=start; addr < endl; addr++) {
        if (breakpoints[addr] & BREAK_PC) {
            p = get_next_symbol_by_value(NULL, addr);
            printf("%.4x  %s\n", addr, p ? p->name : "");
        }
    }
    puts("Memory (r)ead breakpoints:");
    for (addr=start; addr < endl; addr++) {
        if (breakpoints[addr] & BREAK_READ) {
            printf("%.4x", addr);
            for (start=addr; breakpoints[addr] & BREAK_READ; addr++) /**/ ;
            if (--addr != start) printf(".%.4x", addr);
            puts("");
        }
    }
    puts("Memory (w)rite breakpoints:");
    for (addr=start; addr < endl; addr++) {
        if (breakpoints[addr] & BREAK_WRITE) {
            printf("%.4x", addr);
            for (start=addr; breakpoints[addr] & BREAK_WRITE; addr++) /**/ ;
            if (--addr != start) printf(".%.4x", addr);
            puts("");
        }
    }
}

void cmd_set() {
    char *name;
    int v;
    name = parse_delim();
    if (!name) {
        puts("Missing register/flag, expected one of a,x,y,sp,pc or n,v,b,d,i,z,c");
        return;
    }
    if (E_OK != parse_int(&v, DEFAULT_REQUIRED) || E_OK != parse_end()) return;
    if (E_OK != set_reg_or_flag(name, v))
        puts("Unknown register/flag, expected one of a,x,y,sp,pc or n,v,b,d,i,z,c");
}

void cmd_ticks() {
    /* set or show ticks */
    int v, err;

    if (E_OK == (err = parse_int(&v, DEFAULT_OPTIONAL))) {
        ticks = v;
    } else if (E_MISSING == err) {
        printf("%ld ticks\n", ticks);
    }
}

void cmd_fill() {
    uint16_t start, end;
    uint8_t v;
    int addr, endl, err;

    if (E_OK != parse_range(&start, &end, DEFAULT_REQUIRED, 1)) return;

    org = start;

    endl = end < start ? 0x10000 : end;
    if (start == end) {
        puts("Empty range, nothing to fill");
        return;
    }
    addr = start;

    if (E_OK != parse_byte(&v, 0)) return;
    memory[addr++] = v;
    for(;;) {
        err = parse_byte(&v, DEFAULT_OPTIONAL);
        if (err == E_MISSING) break;
        if (err != E_OK) return;
        memory[addr++] = v;
    }

    /* repeat the pattern until we've filled the range */
    while (addr < endl) {
        memory[addr++] = memory[start++];
    }
}

static char _bits[33];
const char* bitstr(int v) {
    char *p;
    for(p=_bits+31; p>=_bits; p--, v >>= 1)
        *p = (v & 1) ? '1': '0';
    _bits[32] = 0;
    for(p=_bits; (*p == '0') && *(p+1); p++) /**/ ;
    return p;
}

void cmd_convert() {
    /* display one or more expression results in multiple bases */
    int v, err, n;

    for(n=0;;n++) {
        err = parse_int(&v, DEFAULT_OPTIONAL);
        if (err == E_MISSING) break;
        if (E_OK != err) return;
        printf(
            "%.*s\t:=  $%x  #%d  %%%s",
            parsed_length(), parsed_str(), v, v, bitstr(v)
        );
        if (32 <= v && v < 127)
            printf("  '%c", v);
        puts("");
    }
    if (!n) puts("No values to convert");
}

void cmd_label() {
    const char *lbl;
    uint16_t addr;

    lbl = parse_delim();
    if (
        !lbl
        || isdigit(lbl[0])
        || strlen(lbl) != strspn(lbl, "_0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")
    ) {
        puts("Invalid label");
        return;
    }
    if (E_OK != parse_addr(&addr, pc) || E_OK != parse_end()) return;

    add_symbol(lbl, addr);
}

void cmd_unlabel() {
    uint16_t addr;

    if (E_OK != parse_addr(&addr, DEFAULT_REQUIRED) || E_OK != parse_end()) return;

    if (0 == remove_symbols_by_value(addr))
        printf("No labels for $%.4x\n", addr);

}

void cmd_blockfile() {
    char *p = parse_delim();
    if (E_OK != parse_end()) return;

    if(!p) puts("Missing block file name");
    else io_blkfile(p);
}

void cmd_load() {
    const char* fname = parse_delim();
    uint16_t addr;

    if (!fname) {
        puts("Missing rom file name");
        return;
    }
    if (E_OK != parse_addr(&addr, DEFAULT_REQUIRED) || E_OK != parse_end()) return;

    (void)load_memory(fname, addr);
    org = addr;
}

void cmd_save() {
    const char* fname = parse_delim();
    uint16_t start, end;

    if (!fname) {
        puts("Missing file name");
        return;
    }
    if (E_OK != parse_range(&start, &end, 0, 0) || E_OK != parse_end()) return;

    if (start == end) end = 0;
    (void)save_memory(fname, start, end);
    org = start;
}

void cmd_heatmap() {
    const char *fname = parse_delim();
    if (!fname) {
        puts("Missing file name");
        return;
    }
    (void)save_heatmap(fname);
}

void cmd_quit() {
    if (E_OK != parse_end()) return;
    monitor_exit();
    exit(0);
}


void cmd_help() {
    int i;
    puts("\nAvailable commands:\n\n");
    for (i=0; 0 != strcmp(_cmds[i].name, "?"); i++)
        printf("  %s %s\n", _cmds[i].name, _cmds[i].help);

    puts(
        "\n"
        "Type q to exit. Use ctrl-C to interrupt the simulator.\n"
        "Commands can be shortened to their first few letters, searched in the order above.\n"
        "For example 'd' means 'disassemble' while 'de' becomes 'delete'.\n"
        "Tab completion and command history (up/down) are also available.\n"
        "Enter repeats commands like step, next, dis or mem as they advance through memory.\n"
        "Literal values are assumed to be hex, e.g. 123f, unless prefixed as binary (%),\n"
        "decimal (#), hex ($) or an ascii character ('). Use the ~ command as a calculator\n"
        "to display values or expression results in all bases. C-style expressions can be used\n"
        "for most address and value arguments.  Expressions can include (case-sensitive) labels\n"
        "as well as CPU registers A, X, Y, PC, SP and flags N, V, B, D, I, Z and C.\n"
        "The usual operators are available, along with * and @ to deference memory by byte or word\n"
        "and unary < and > to extract least and most significant bytes like most assemblers.\n"
        "For example >(@(*(pc+1))+x) takes the high byte of an indirect zp address via a PC operand.\n"
        "Write ranges as start.end or start..offset, e.g. 1234.1268, pc .. 10, 1234, ..20, .label.\n"
        "Think of the second . as a shortcut: start..offset is the same as start.start+offset\n"
    );
}

Command _cmds[] = {
    { "go", "[addr] - run from pc (or optional addr) until breakpoint", 0, cmd_go },
    { "continue [addr]", "- run from pc until breakpoint (or optional addr)", 0, cmd_continue },
    { "step", "- [count] step by single instructions", 1, cmd_step },
    { "next", "- [count] like step but treats jsr ... rts as one step", 1, cmd_next },
    { "call", "addr - call subroutine leaving PC unchanged", 0, cmd_call },

    { "disassemble", "[range] - show code disassembly for range (or current)", 1, cmd_disasm },
    { "memory", "[range] - dump memory contents for range (or current)", 1, cmd_memory },
    { "stack", "- show stack contents, sp+1 through $1ff", 0, cmd_stack },
    { "break",  "[range] r|w|a|x - trigger break on read, write, any access or execute (default)", 0, cmd_break },
    { "delete",  "[range] - remove all breakpoints in range (default PC)", 0, cmd_delete },
    { "show", "[range] - show all breakpoints in range (default all)", 0, cmd_show },

    { "set", "{a|x|y|sp|pc|n|v|d|i|z|c} value - modify a register or flag", 0, cmd_set },
    { "ticks", "[value] - query or set the current cycle count", 0, cmd_ticks },
    { "fill", "range value ... - set memory contents in range to value(s)", 0, cmd_fill },
    { "~", "value ... - show each value in binary, octal, hex and decimal", 0, cmd_convert },
    { "label", "name addr - add a symbolic name for addr", 0, cmd_label },
    { "unlabel", "name|addr - remove label by name or addr", 0, cmd_unlabel },

    { "load", "romfile addr - read binary file to memory", 0, cmd_load },
    { "save", "romfile [range] - write memory to file (default full dump)", 0, cmd_save },
    { "heatmap", "mapfile - dump memory usage to files mapfile-[aw].dat", 0, cmd_heatmap },
    { "blockfile", "[blockfile] - use binary file for block storage, empty to disable", 0, cmd_blockfile },
    { "quit", "- leave c65", 0, cmd_quit },
    { "help", "or ? - show this help", 0, cmd_help },
    { "?", "", 0, cmd_help },    // this must be last
};

#define n_cmds sizeof(_cmds)/sizeof(_cmds[0])

Command *_repeat_cmd = NULL;

void do_cmd(char *line) {
    char* p;
    Command *cmd;
    int i;

    /* strip comments starting with ; */
    p = strpbrk(line, ";");
    if (p) *p = 0;
    parse_start(line);
    p = parse_delim();
    if (!p) return;

    for(i=0; i<n_cmds; i++) {
        /* check for case insenstive match */
        if(!strncasecmp(p, _cmds[i].name, strlen(p))) {
            cmd = _cmds+i;
            _repeat_cmd = cmd->repeatable ? cmd: NULL;
            cmd->handler();
            return;
        }
    }
    puts("Unknown command, try ? for help");
}


void completion(const char *buf, linenoiseCompletions *lc, void *user_data) {
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
    char buf[128], *s;
    const char *label;
    int fail=0, tmp;
    linenoiseSetCompletionCallback(completion, NULL);
    linenoiseHistorySetMaxLen(256);
    linenoiseHistoryLoad(".c65");

    if (labelfile) {
        f = fopen(labelfile, "r");
        if (!f) {
            printf("Can't read labels from %s\n", labelfile);
            return;
        }
        while (fgets(buf, sizeof(buf), f) > 0) {
            label = strtok(buf, "\t =");
            s = strtok(NULL, "\t =\n");
            /*
                tailored to 64tass --labels output with 'symbol\t=\t$1234
                enumerated constants are emitted in similar format, eg. with two digits
                but we want to ignore those
            */
            if (s && s[0] == '$' && strlen(s) == 5 && (E_OK == strexpr(s+1, &tmp))) {
                add_symbol(label, (uint16_t)tmp);
            } else {
                fail++;
            }
        }
        if (fail) printf("Skipped %d lines from %s\n", fail, labelfile);
        fclose(f);
    }
    puts("Type ? for help, ctrl-C to interrupt, quit to exit.");
}

void monitor_exit() {
    linenoiseHistorySave(".c65");
}

void monitor_command() {
    char *line;

    if (step_mode != STEP_NONE) {
        org = pc;
        step_mode = STEP_NONE;
        if (break_flag & (BREAK_READ | BREAK_WRITE)) {
            printf("%.4x: memory %s\n", rw_brk, break_flag & BREAK_READ ? "read": "write");
            dump(rw_brk & 0xfff0, rw_brk | 0xf);
        }
        disasm(pc, pc+1);
    }

    line = linenoise(prompt());
    if(strlen(line)) {
        linenoiseHistoryAdd(line);
        do_cmd(line);
        free(line);
    } else if (_repeat_cmd) {
        /* empty line can repeat some commands */
        _repeat_cmd->handler();
    }
}

