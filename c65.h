/* break conditions used in breakpoints */
#define BREAK_READ  1
#define BREAK_WRITE 2
#define BREAK_PC    4
#define BREAK_ONCE  8
/* special break conditions */
#define BREAK_BRK 16
#define BREAK_INT 32
#define BREAK_EXIT 64

#define STEP_NONE 0
#define STEP_INST 1
#define STEP_NEXT 2
#define STEP_OVER 3
#define STEP_RUN 4

extern uint8_t memory[65536];
extern uint8_t breakpoints[65536];

extern uint16_t pc;
extern uint8_t a, x, y, sp, status;
extern uint16_t rw_brk;

extern uint64_t heatrs[65536], heatws[65536];

extern uint64_t ticks;
extern int break_flag, step_mode, step_target;

const char* opname(uint8_t op);
uint8_t oplen(uint8_t op);
const char* opfmt(uint8_t op);

int get_reg_or_flag(const char *name);
int set_reg_or_flag(const char *name, int v);

int load_memory(const char* romfile, int addr);
int save_memory(const char* romfile, uint16_t start, uint16_t end);
int save_heatmap(const char* heatfile);
