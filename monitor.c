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

#define FLAG_FMT(f) (status & FLAG_##f ? TXT_B3 : TXT_LO)

char* prompt() {
    sprintf(_prompt,
        TXT_LO "PC" TXT_N " %.4x  "
        "%sN" TXT_N "%sV" TXT_N TXT_LO "-" TXT_N "%sB" TXT_N
        "%sD" TXT_N "%sI" TXT_N "%sZ" TXT_N "%sC" TXT_N "  "
        TXT_LO "A " TXT_N "%.2x " TXT_LO "X " TXT_N "%.2x " TXT_LO "Y " TXT_N "%.2x " TXT_LO "SP " TXT_N "%.2x "
        TXT_LO "> " TXT_N,
        pc,
        FLAG_FMT(SIGN), FLAG_FMT(OVERFLOW), /* - */ FLAG_FMT(BREAK),
        FLAG_FMT(DECIMAL), FLAG_FMT(INTERRUPT), FLAG_FMT(ZERO), FLAG_FMT(CARRY),
        a, x, y, sp
    );
    return _prompt;
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

    /* first look for a label with matching name */
    for (prv=NULL, l=labels; l && 0 != strcmp(l->name, name_or_addr); prv=l, l=l->next) /**/ ;
    if (l) {
        if (prv) prv->next = l->next;
        else labels = l->next;
        free((void*)l->name);
        free(l);
        return;
    }
    /* otherwise try parsing as address */
    addr = strtol(name_or_addr, &q, 16);
    if (q == name_or_addr) {
        puts("No matching name or address");
        return;
    }

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

int _parse_addr(char *p, uint16_t *addr, int dflt) {
    /* parse address, with default if >= 0 */
    /* return 0 on success else error code */

    char *q;
    int v;

    if (!p) {
        if (dflt < 0) {
            puts("Missing address");
            return E_MISSING;
        }
        *addr = (uint16_t)dflt;
    } else {
        v = address_for_label(p);
        if (v < 0) {
            v = strtol(p, &q, 16);
            if (q==p) {
                printf("Couldn't parse address '%s'\n", p);
                return E_PARSE;
            }
        }
        *addr = (uint16_t)v;
    }
    return E_OK;
}

int parse_addr(uint16_t *addr, int dflt) {
    char *p = strtok(NULL, " ");
    return _parse_addr(p, addr, dflt);
}

int parse_range(uint16_t* start, uint16_t* end, int dflt_start, int dflt_length) {
    /* parse a range witten as a single string start:end or start/offset  */
    char *p, *q, sep;
    uint16_t dflt_end;
    int v;

    p = strtok(NULL, " ");
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
            v = _parse_addr(p, start, dflt_start);
            if (v!=0) return v;
        }
        dflt_end = *start + (uint16_t)dflt_length;
        switch (sep) {
            case ':':
                v =  _parse_addr(++q, end, dflt_length < 0 ? -1 : dflt_end);
                if (v!=0) return v;
                break;
            case '/':
                v = strtol(++q, &p, 16);
                if (p == q) {
                    printf("Couldn't parse offset '%s'\n", q);
                    return E_PARSE;
                }
                *end = *start + v;
                break;
            default:
                /* only start address was given */
                if (dflt_length < 0) {
                    puts("Missing :end or /offset for range");
                    return E_MISSING;
                }
                *end = dflt_end;
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
    uint16_t addr;
    /*
    0          1          2         3
    012345678 9012345678 90123456789012345
       abcd  <aa bb cc  >bbr3 $zp,$rr
    *B abce  <aa bb     >bne  $rr
       abdf  <aa bb cc  >lda  ($abcd,x)
    */
    for (addr = start; addr < end; /**/){
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
    uint16_t addr = start & 0xfff0;
    char line[256], chrs[16], *p;
    uint8_t c, v;

    while(addr < end) {
        memset(chrs, ' ', 16);
        p = line;
        /* show the address */
        p += sprintf(p, "%.4x  ", addr);
        /* show blanks before truncated address */
        while (addr < start) p += sprintf(p, "   %s", (++addr & 0xf) == 8 ? " ":"");

        do {
            if (addr < end) {
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
        } while((addr & 0xf) && addr < end);
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
    char *p = strtok(NULL, " "), *q;
    int v;
    disasm(pc, pc+1);
    step_mode = mode;
    step_target = 1;
    if (p) {
        v = strtol(p, &q, 16);
        if (q != p) step_target = v;
    }
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

    if (0 != parse_range(&start, &end, pc, 1)) return;

    p = strtok(NULL, " ");
    switch(p?tolower(*p):'x') {
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
        for(/**/; start < end; start++)
            breakpoints[start] |= f;
        org = start;
    } else {
        printf("Unknown breakpoint type '%c'\n", *p);
    }
}


void cmd_delete() {
    uint16_t start, end;

    if (0 != parse_range(&start, &end, pc, 1)) return;

    for(/**/; start < end; start++)
        breakpoints[start] = 0;
    org = start;
}


void cmd_set() {
    const char *vars = "axysp nv bdizc";    /* 0-4 are regs, 6-13 are flags*/
    uint8_t* regs[] = {&a, &x, &y, &sp}, i, v, bit;
    char *p, *q;
    p = strtok(NULL, " ");
    if (!p) {
        puts("Missing register/flag, expected one of a,x,y,sp,pc or n,v,b,d,i,z,c");
        return;
    }
    q = strchr(vars, tolower(*p));
    if (!q) {
        puts("Unknown register/flag, expected one of a,x,y,sp,pc or n,v,b,d,i,z,c");
        return;
    }
    i = q-vars;
    p = strtok(NULL, " ");
    if (!p) {
        puts("Missing value in set");
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

    if (0 != parse_range(&start, &end, pc, 1)) return;

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
        || strlen(lbl) != strspn(lbl, "_@0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")
    ) {
        puts("Invalid label");
        return;
    }
    if (0 == parse_addr(&addr, pc))
        add_label(lbl, addr);
}

void cmd_unlabel() {
    const char *p = strtok(NULL, " ");
    if(!p) puts("Missing label name or address");
    else del_labels(p);
}

void cmd_blockfile() {
    const char* p = strtok(NULL, " ");
    if(!p) puts("Missing block file name");
    else io_blkfile(p);
}

void cmd_load() {
    const char* fname = strtok(NULL, " ");
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
    const char* fname = strtok(NULL, " ");

    if (!fname) {
        puts("Missing file name");
        return;
    }
    if (0 != parse_range(&start, &end, 0, 0)) return;

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
    puts("\nAvailable commands:\n\n");
    for (i=0; 0 != strcmp(_cmds[i].name, "?"); i++)
        printf("  %s %s\n", _cmds[i].name, _cmds[i].help);

    puts(
        "\n"
        "Use ctrl-C to interrupt run or continue, type q or quit to exit.\n"
        "Commands will auto-complete from their initial characters.  For example\n"
        "'d' becomes 'disassemble' while 'de' becomes 'delete'.  Tab completion is available\n"
        "along with command history using the up and down arrows.\n"
        "Repeat commands like step, next, disassemble or memory by just hitting enter.\n"
        "The monitor maintains a current address so repeating (say) mem advances through memory.\n"
        "Write all addresses and values in hex with no prefix, e.g. 123f.\n"
        "Labels can substituted for any address, including the special 'pc'.\n"
        "Ranges are written as start:end or start/offset with no spaces, e.g. 1234:1268, 1234/10, /20.\n"
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
    { "store", "range value ... - set memory contents in range to value(s)", 0, cmd_store },
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
    char* p = strtok(line, " ");
    Command *cmd;
    int i;

    for(i=0; i<n_cmds; i++) {
        if(!strncmp(p, _cmds[i].name, strlen(p))) {
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
    char buf[128], *s, *end;
    const char *label;
    uint16_t addr;
    int fail=0;
    linenoiseSetCompletionCallback(completion, NULL);
    linenoiseHistorySetMaxLen(256);
    linenoiseHistoryLoad(".c65");

    if (labelfile) {
        f = fopen(labelfile, "r");
        if (!f) {
            printf("Couldn't read labels from %s\n", labelfile);
            return;
        }
        while (fgets(buf, sizeof(buf), f) > 0) {
            label = strtok(buf, "\t =");
            s = strtok(NULL, "\t =\n");
            end = 0;
            /*
                tailored to 64tass --labels output with 'symbol\t=\t$1234
                enumerated constants are emitted in similar format, eg. with two digits
                but we want to ignore those
            */
            if (s && s[0] == '$' && strlen(s) == 5) addr = strtol(s+1, &end, 16);
            if (s && end > s+1) {
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

