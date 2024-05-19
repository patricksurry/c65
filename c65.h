#define BREAK_ALWAYS  1
#define BREAK_ONCE  2
#define BREAK_EXECUTE (BREAK_ALWAYS | BREAK_ONCE)
#define BREAK_READ  4
#define BREAK_WRITE 8
#define BREAK_ACCESS (BREAK_READ | BREAK_WRITE)
#define BREAK_SHUTDOWN 128

#define STEP_NONE 0
#define STEP_RUN 1
#define STEP_INST 2
#define STEP_JSR 3

extern uint8_t memory[65536];
extern uint8_t breakpoints[65536];

extern uint16_t pc;
extern uint8_t a, x, y, sp, status;

extern long ticks;
extern int break_flag, step_mode, step_target;

const char* opname(uint8_t op);
uint8_t oplen(uint8_t op);
const char* opfmt(uint8_t op);

int load_memory(const char* romfile, int addr);
int save_memory(const char* romfile, uint16_t start, uint16_t end);
