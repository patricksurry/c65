extern int io_addr;

void io_init(int debug);
void io_exit();

FILE* io_blkfile(const char *fname);
void io_magic_read(uint16_t addr);
void io_magic_write(uint16_t addr, uint8_t);
