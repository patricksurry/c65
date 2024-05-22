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

/*
Linked list of symbolic address labels.
We can have multiple labels for a single address,
but require that label names are unique.
*/
typedef struct Label {
    const char *name;
    uint16_t addr;
    struct Label* next;
} Label;

typedef struct Command {
    const char *name;
    const char *help;
    int repeatable;
    void (*handler)();
} Command;

#define E_OK 0
#define E_MISSING 1
#define E_PARSE 2

Label *labels = NULL;
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


int _str2val(const char *p, uint16_t *value, int dflt, const char* kind) {
    /* parse word value, with default if >= 0 */
    /* return 0 on success else error code */

    char *q;
    int v, base=16;

    if (!p) {
        if (dflt < 0) {
            printf("Missing %s\n", kind);
            return E_MISSING;
        }
        *value = (uint16_t)dflt;
    } else if (*p == 0x27) {  /* single quote */
        *value = *++p;
    } else {
        switch (*p) {
            case '#':
                base=10; p++; break;
            case '%':
                base=2; p++; break;
            case '&':
                base=8; p++; break;
            case '$':
                base=16; p++; break;
        }
        v = strtol(p, &q, base);
        if (*q) {
            printf("Invalid %s '%s'\n", kind, p);
            return E_PARSE;
        }
        *value = (uint16_t)v;
    }
    return E_OK;
}


void add_label(const char* name, uint16_t addr) {
    Label *l, *prv;

    /* first discard any existing label with the same name */
    for (prv=NULL, l=labels; l; prv=l, l=l->next) {
        if (0==strcmp(l->name, name)) {
            if (prv) prv->next = l->next;
            else labels = l->next;
            free((void*)l->name);
            free(l);
            break;
        }
    }

    /* add the new label to head of list*/
    l = malloc(sizeof(Label));
    l->name = strdup(name);
    l->addr = addr;
    l->next = labels;
    labels = l;
}

void del_labels(const char *name_or_addr) {
    Label *prv, *l;
    char *q;
    uint16_t addr;

    /* first check for label with matching name */
    for (prv=NULL, l=labels; l && 0 != strcmp(l->name, name_or_addr); prv=l, l=l->next) /**/ ;
    if (l) {
        if (prv) prv->next = l->next;
        else labels = l->next;
        free((void*)l->name);
        free(l);
        return;
    }
    /* otherwise try parsing as address */
    if (E_OK != _str2val(name_or_addr, &addr, -1, "address")) return;

    /* remove all labels matching addr */
    for (prv=NULL, l=labels; l; prv=l, l=l->next) {
        if(l->addr == addr) {
            if (prv) prv->next = l->next;
            else labels = l->next;
            free((void*)l->name);
            free(l);
            l = prv ? prv : labels;
            q = NULL;
        }
    }
    if (q) printf("No labels for $%.4x\n", addr);
}

const char* label_for_address(uint16_t addr) {
    /* return first label name matching address */
    Label *l;

    for (l=labels; l; l=l->next) {
        if (l->addr == addr) return l->name;
    }
    return NULL;
}

int address_for_label(const char* name) {
    Label *l;

    if (0 == strcasecmp(name, "pc")) return pc;
    for (l=labels; l; l=l->next)
        if (strcmp(name, l->name) == 0)
            return l->addr;
    return -1;
}


int _str2addr(const char *p, uint16_t *addr, int dflt) {
    /* parse addr value (numeric or label), with default if >= 0 */
    /* return 0 on success else error code */
    int v;

    if (!p) {
        if (dflt < 0) {
            puts("Missing address");
            return E_MISSING;
        }
        *addr = (uint16_t)dflt;
    } else {
        /* is it a label? */
        v = address_for_label(p);
        if (v < 0) {
            /* maybe a plain address? */
            return _str2val(p, addr, dflt, "address");
        }
        *addr = (uint16_t)v;
    }
    return E_OK;
}

int parse_addr(uint16_t *addr, int dflt) {
    char *p = strtok(NULL, " \t");
    return _str2addr(p, addr, dflt);
}

int parse_range(uint16_t* start, uint16_t* end, int dflt_start, int dflt_length) {
    /* parse a range witten as a single string start:end or start/offset  */
    char *p, *q, sep;
    uint16_t offset;
    int v;

    p = strtok(NULL, " \t");
    if (!p) {
        if (dflt_start < 0 || dflt_length < 0) {
            puts("Missing range");
            return E_MISSING;
        }
        *start = (uint16_t)dflt_start;
        *end = (uint16_t)(dflt_start + dflt_length);
    } else {
        q = strpbrk(p, "/:");
        sep = q ? *q: 0;
        if (p == q) {
            if (dflt_start < 0) {
                puts("Missing start for range");
                return E_MISSING;
            }
            *start = (uint16_t)dflt_start;
        } else {
            if (q) *q = 0;   // terminate p so we can parse addr
            v = _str2addr(p, start, dflt_start);
            if (v!=0) return v;
        }
        switch (sep) {
            case ':':
                v =  _str2addr(++q, end, -1);
                if (v!=0) return v;
                break;
            case '/':
                v = _str2val(++q, &offset, -1, "offset");
                if (v != E_OK) return v;
                *end = *start + offset;
                break;
            default:
                /* only start address was given */
                if (dflt_length < 0) {
                    puts("Missing :end or /offset for range");
                    return E_MISSING;
                }
                *end = *start + (uint16_t)dflt_length;
        }
    }
    return E_OK;
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
    const char *fmt;
    Label *l;
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
        for (l=labels; l; l=l->next)
            if (l->addr == addr) printf("%s:\n", l->name);

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
    if(0 == parse_addr(&pc, pc)) {
        step_mode = STEP_RUN;
    }
}


void cmd_continue() {
    /* run imdefinitely, optionally to one-time breakpoint */
    uint16_t addr;
    if(0 == parse_addr(&addr, pc)) {
        if (addr != pc) breakpoints[addr] |= BREAK_ONCE;
        step_mode = STEP_RUN;
    }
}

void _cmd_single(int mode) {
    char *p = strtok(NULL, " \t");
    uint16_t steps = 1;
    disasm(pc, pc+1);
    step_mode = mode;
    if (p && (E_OK != _str2val(p, &steps, -1, "steps"))) return;
    step_target = steps;
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

    if (0 != parse_addr(&target, -1))
        return;

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

    if (0 != parse_range(&start, &end, org, 16)) return;

    org = disasm(start, end);
}


void cmd_memory() {
    uint16_t start, end;

    if (0 != parse_range(&start, &end, org, 64)) return;
    dump(start, end);
    org = end;
}


void cmd_stack() {
    dump(sp + 0x101, 0x200);
}


void cmd_break() {
    uint16_t start, end;
    uint8_t f=0;
    char *p;
    int endl;

    if (0 != parse_range(&start, &end, pc, 1)) return;

    endl = end < start ? 0x10000 : end;

    p = strtok(NULL, " \t");
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
        for(/**/; start < endl; start++)
            breakpoints[start] |= f;
        org = start;
    } else {
        printf("Unknown breakpoint type '%c'\n", *p);
    }
}


void cmd_delete() {
    uint16_t start, end;
    int endl;

    if (0 != parse_range(&start, &end, pc, 1)) return;

    endl = end < start ? 0x10000 : end;

    for(/**/; start < endl; start++)
        breakpoints[start] = 0;
    org = start;
}


void cmd_set() {
    const char *vars = "axysp nv bdizc";    /* 0-4 are regs, 6-13 are flags*/
    uint8_t *regs[] = {&a, &x, &y, &sp}, i, bit;
    uint16_t v;
    char *p, *q;
    p = strtok(NULL, " \t");
    if (!p) {
        puts("Missing register/flag, expected one of a,x,y,sp,pc or n,v,b,d,i,z,c");
        return;
    }
    q = strchr(vars, tolower(*p));
    if (!q) {
        puts("Unknown register/flag, expected one of a,x,y,sp,pc or n,v,b,d,i,z,c");
        return;
    }
    i = q - vars;
    p = strtok(NULL, " \t");
    if (E_OK != _str2val(p, &v, -1, "value")) return;

    if (i < 4) {
        *(regs[i]) = v;
    } else if (i==4) { /* PC is a special case */
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

void cmd_fill() {
    uint16_t start, end, addr, val;
    int endl;
    char *p;

    if (0 != parse_range(&start, &end, -1, 1)) return;

    org = start;

    endl = end < start ? 0x10000 : end;
    if (start == end) {
        puts("Empty range, nothing to fill");
        return;
    }
    addr = start;
    while((p = strtok(NULL, " \t"))) {
        if (E_OK != _str2val(p, &val, -1, "value")) return;
        memory[addr++] = (uint8_t)val;
    }
    /* with no values, default to zero fill */
    if (addr == start) {
        memory[addr++] = 0;
    }
    /* repeat the pattern until we've filled the range */
    while (addr < endl) {
        memory[addr++] = memory[start++];
    }
}

const char* bitstr(uint16_t v) {
    char buf[17], *p;
    for(p=buf+15; p>=buf; p--, v >>= 1)
        *p = v & 1 ? '1': '0';
    buf[16] = 0;
    for(p=buf; *p == '0'; p++) /**/ ;
    return p;
}

void cmd_convert() {
    char *p;
    int n=0;
    uint16_t val;

    while((p = strtok(NULL, " \t"))) {
        n++;
        if (E_OK != _str2val(p, &val, -1, "value")) return;
        printf("%s\t$%x  #%d  &%o  %%%s", p, val, val, val, bitstr(val));
        if (32 <= val && val < 127) printf("  '%c", val);
        puts("");
    }
    if(!n) puts("No values to convert");
}

void cmd_label() {
    const char *lbl = strtok(NULL, " \t");
    uint16_t addr;

    if (
        !lbl
        || isdigit(lbl[0])
        || strlen(lbl) != strspn(lbl, "_@0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")
    ) {
        puts("Invalid label");
        return;
    }
    if (0 == parse_addr(&addr, pc))
        add_label(lbl, addr);
}

void cmd_unlabel() {
    const char *p = strtok(NULL, " \t");
    if(!p) puts("Missing label name or address");
    else del_labels(p);
}

void cmd_blockfile() {
    const char* p = strtok(NULL, " \t");
    if(!p) puts("Missing block file name");
    else io_blkfile(p);
}

void cmd_load() {
    const char* fname = strtok(NULL, " \t");
    uint16_t addr;

    if (!fname) {
        puts("Missing rom file name");
        return;
    }
    if (0 == parse_addr(&addr, -1)) {
        (void)load_memory(fname, addr);
        org = addr;
    }
}

void cmd_save() {
    uint16_t start, end;
    const char* fname = strtok(NULL, " \t");

    if (!fname) {
        puts("Missing file name");
        return;
    }
    if (0 != parse_range(&start, &end, 0, 0)) return;

    if (start == end) end = 0;
    (void)save_memory(fname, start, end);
    org = start;
}


void cmd_quit() {
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
        "Commands auto-complete from their initial characters in the order above.\n"
        "For example 'd' means 'disassemble' while 'de' becomes 'delete'.\n"
        "Tab completion and command history (up/down) are also available.\n"
        "Enter repeats commands like step, next, dis or mem as they advance through memory.\n"
        "Values are normally written as hex, e.g. 123f, or with an explicit prefix for\n"
        "binary (%), octal (&), decimal (#), hex ($) or printable ascii (').\n"
        "The ~ command can be useful to show values in different bases.\n"
        "Case sensitive labels (and the special 'pc') can be used as addresses but not values.\n"
        "Write ranges as start:end or start/offset, e.g. 1234:1268, pc/10, 1234, /20, :label.\n"
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
    { "delete",  "[range] - remove all breakpoints in range (default all)", 0, cmd_delete },
    { "set", "{a|x|y|sp|pc|n|v|d|i|z|c} value - modify a register or flag", 0, cmd_set },
    { "fill", "range value ... - set memory contents in range to value(s)", 0, cmd_fill },
    { "~", "value ... - show each value in binary, octal, hex and decimal", 0, cmd_convert },
    { "label", "name addr - add a symbolic name for addr", 0, cmd_label },
    { "unlabel", "name|addr - remove label by name or addr", 0, cmd_unlabel },

    { "load", "romfile addr - read binary file to memory", 0, cmd_load },
    { "save", "romfile [range] - write memory to file (default full dump)", 0, cmd_save },
    { "blockfile", "[blockfile] - use binary file for block storage, empty to disable", 0, cmd_blockfile },
    { "quit", "- leave c65", 0, cmd_quit },
    { "help", "or ? - show this help", 0, cmd_help },
    { "?", "", 0, cmd_help },    // this must be last
};

#define n_cmds sizeof(_cmds)/sizeof(_cmds[0])

Command *_repeat_cmd = NULL;

void parse_cmd(char *line) {
    char* p;
    Command *cmd;
    int i;

    /* strip comments starting with ; */
    p = strpbrk(line, ";");
    if (p) *p = 0;
    p = strtok(line, " \t");
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
    uint16_t addr;
    int fail=0;
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
            if (s && s[0] == '$' && strlen(s) == 5 && (E_OK == _str2val(s+1, &addr, -1, "address"))) {
                add_label(label, addr);
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
    }

    line = linenoise(prompt());
    if(strlen(line)) {
        linenoiseHistoryAdd(line);
        parse_cmd(line);
        free(line);
    } else if (_repeat_cmd) {
        /* empty line can repeat some commands */
        _repeat_cmd->handler();
    }
}

