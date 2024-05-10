// PROGRAMMERS : Patrick Surry and Sam Colwell
// FILE        : io.c
// DATE        : 2024-05
// DESCRIPTION : This abstracts the I/O and allows supporting multiple
// environments (Linux, OSX, WSL, Native Windows) that all have gcc.
#ifdef WINDOWS_NATIVE
#include <stdio.h>
#include <conio.h> // Windows specific

void set_terminal_nb() {} // No-op
//int _kbhit(); // _kbhit already available in conio.h
int _getc() { return getch(); } // getch() from conio.h has no echo.
void _putc(char ch) { putchar(ch); fflush(stdout); return; }
#else
// These should work on Linux, OSX, and WSL.
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void reset_terminal() { tcsetattr(0, TCSANOW, &orig_termios); }
void signal_handler() { exit(0); /* calls atexit handlers */ }

void set_terminal_nb() {
  struct termios new_termios;

  /* take two copies - one for now, one for later */
  tcgetattr(0, &orig_termios);
  memcpy(&new_termios, &orig_termios, sizeof(new_termios));

  /* register cleanup handler, and set the new terminal mode */
  atexit(reset_terminal);
  signal(SIGINT, signal_handler);

  /* see https://man7.org/linux/man-pages/man3/tcsetattr.3.html */
  /* we want something close to cfmakeraw but we'll keep ONLCR and SIGINT etc */
  new_termios.c_iflag &= ~(PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
  new_termios.c_oflag |= OPOST | ONLCR;
  new_termios.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN);
  new_termios.c_cflag &= ~(CSIZE | PARENB);
  new_termios.c_cflag |= CS8;

  tcsetattr(0, TCSANOW, &new_termios);

  setbuf(stdout, NULL); /* unbuffered output */
}

/*
compatibility with windows _kbhit, return non-zero if key ready
see https://stackoverflow.com/questions/448944/c-non-blocking-keyboard-input
*/
int _kbhit() {
  struct timeval tv = {0L, 0L};
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(0, &fds);
  return select(1, &fds, NULL, NULL, &tv) > 0;
}

/* non-blocking version of getch() */
int _getc() {
  int r;
  unsigned char c;
  r = read(0, &c, sizeof(c));
  //  if (c == 4)
  //    r = -1; /* ctrl-D => eof */
  //  if (c == 3)
  //    raise(SIGINT);
  return r < 0 ? r : c;
}

void _putc(char ch) { putchar((int)ch); }
#endif
