/* access modes */
#define MONITOR_READ         1
#define MONITOR_WRITE        2
#define MONITOR_DATA         (MONITOR_READ|MONITOR_WRITE)
#define MONITOR_PC           4
#define MONITOR_ANY          (MONITOR_PC|MONITOR_DATA)
#define MONITOR_ONCE         8       /* flag to clear on hit */

/* special break conditions */
#define MONITOR_BRK          16      /* BRK instruction */
#define MONITOR_SIGINT       32      /* break back to */
#define MONITOR_EXIT         64      /* exit simulation */

#define STEP_NONE 0
#define STEP_INST 1
#define STEP_NEXT 2
#define STEP_OVER 3
#define STEP_RUN 4

extern uint8_t memory[0x10000];
extern uint8_t breakpoints[0x10000];

extern uint16_t pc;
extern uint8_t a, x, y, sp, status;
extern uint16_t rw_brk;

extern uint64_t heat_rs[0x10000], heat_ws[0x10000], heat_xs[0x10000];

extern uint64_t ticks;
extern int break_flag, step_mode, step_target;

const char* opname(uint8_t op);
uint8_t oplen(uint8_t op);
const char* opfmt(uint8_t op);

int get_reg_or_flag(const char *name);
int set_reg_or_flag(const char *name, int v);

int load_memory(const char* romfile, int addr);
int save_memory(const char* romfile, uint16_t start, uint16_t end);

extern void reset6502();
extern void irq6502();
extern void nmi6502();