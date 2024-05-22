`c65` provides a simple 65c02 simulator based on
[`fake65c02.h`](https://github.com/C-Chads/MyLittle6502)
that mimics [`pymon`](https://github.com/mnaberez/py65)'s
default magic getc and putc interface.
`c65` also provides a magic [blockio](#blockio) interface
which supports simulator IO to/from an external binary file,
as well as a simple [debugger](#debugger).

## Usage

The simplest usage is:

    c65 -r taliforth-py65mon.bin

This loads the TaliForth2 memory image at the top of memory
and resets to the address stored at $fffc. This corresponds exactly to
`py65mon -m 65c02 -r taliforth-py65mon.bin` without the monitor tools.
Various options control the simulator (use `c65 -?` for current options):

    -a <address>    # load the rom file at a specific address
    -r <address>    # run from address, rather than via the reset vector @ $fffc
    -m <address>    # change the magic IO base address (default $f000)
    -b <file>       # enable blockio using the provided binary file
    -g              # start c65 in the debugger

## Magic IO

`c65` provides a magic IO block that spans a 22 byte range
and is normally based at $f000. Use `-m` to change the base address.
This supports a number of IO functions:

    Addr    Name    Description

    $f001   putc    Write here to send the byte to stdout
    $f004   getc    Read here to block fetching a byte from stdin
    $f005   peekc   Non-blocking read, bit7=1 if ready with 7bit character

    $f006   start   Reading here starts the cycle counter
    $f007   stop    Reading here stops the cycle counter
    $f008-b cycles  Current 32 bit cycle count in NUXI order

    $f010   blkio   Write here to execute a block IO action (see below)
    $f011   status  Read block IO status here
    $f012-3 blknum  Block number to read/write
    $f014-5 buffer  Start of 1024 byte memory buffer to read/write

## Block IO

The base address (default $f010) is the first byte of a six byte interface:

    offset  name    I/O description
    0       action  I   initiate IO action (set other params first)
    1       status  O   returns 0 on success and 0xff otherwise
    2-3     blknum  I   0-indexed low-endian block to read or write
    4-5     bufptr  I   low-endian pointer to 1024 byte buffer to r/w

To initiate a block IO operation, set the `blknum` and `bufptr` parameters
and then write the `action` code to the base address. The `status`
value is returned. Four actions are currently supported:

- status (0): query blkio status: sets `status` to 0x0 if enabled, 0xff otherwise
- read (1): read the 1024 byte block @ `blknum` to `bufptr`
- write (2): write 1024 bytes from `bufptr` to the block @ `blknum`

Note that an external blockfile must be specified with the `-b ...` option
to enable block IO. The file is simply a binary file with block k
mapped to offset k*1024 through (k+1)*1024-1.
The two-byte `blknum` supports a maximum addressable file size of 64Mb.
A portable (cross-platform) check for blkio availability is:
1. write 1 to `status`
2. write 0 to `action`
3. test if `status` is now 0

You can boot from a blkio file by adding the following snippet to
the end of `forth_code/user_words.fs`:

    \ if blkio is available and block 0 starts with the bytes 'TF'
    \ `evaluate` the remainder of block 0 as a zero-terminated string
    \ Tequires the word asciiz> ( addr -- addr n )

    : blkrw ( blk buf action -- )
        -rot $c014 ! $c012 ! $c010 c!
    ;
    :noname
        1 $c011 c! 0 $c010 c! $c011 c@ 0= if  \ blkio available?
            0 $1000 1 blkrw
            $1000 @ $4654 = if                \ starts with magic "TF" ?
                $1002 asciiz> evaluate else   \ run the block
                ." bad boot block" CR
            then else
            ." no block device" CR
        then
    ; execute

## Debugger

`c65` offers a simple debugger which is useful to explore and extend Taliforth.
Enable it with the `-g` option.  You can also provide
a `64tass` label file to import symbolic labels for your code.
For example:

    c65/c65 -g -r taliforth-py65mon.bin -l docs/py65mon-labelmap.txt

should result in something like this:

    c65: reading ../taliforth-py65mon.bin to $8000:$ffff
    c65: PC=f016 A=00 X=00 Y=00 S=fd FLAGS=<N0 V0 B0 D0 I1 Z0 C0> ticks=0
    Skipped 84 lines from ../docs/py65mon-labelmap.txt
    Type ? for help, ctrl-C to interrupt, quit to exit.
    PC f016  NV-BDIZC  A 00 X 00 Y 00 SP fd >

Your actual prompt should be a little more colorful,
particularly to highlight which CPU flags are currently set.
Try typing `d<tab><enter>` to disassemble from the current program counter.
(Actually just `d<enter>` is enough, but sometimes tab completion is handy.)

    PC f016  NV-BDIZC  A 00 X 00 Y 00 SP fd > disassemble
    kernel_init:
    io_end:
    *  f016  78        sei
       f017  a2 00     ldx  #$0
       f019  bd 32 f0  lda  s_kernel_id,x
       f01c  f0 06     beq  $f024 ; +6
       f01e  20 2d f0  jsr  kernel_putc
       f021  e8        inx
       f022  80 f5     bra  $f019 ; -11
       f024  4c 00 80  jmp  xt_cold
    PC f016  NV-BDIZC  A 00 X 00 Y 00 SP fd >

You can continue diassembly with just `<enter>`, repeating the previous command
after advancing the current address.  Persistent command history is available with the up/down arrows,
along with many [Gnu readline](https://en.wikipedia.org/wiki/GNU_Readline)-style key bindings.
For example try `ctrl-R` to search previous commands.

You can disassemble a specific
range with `d xt_cold/20` (the `/20` means a range of $20 bytes) or `d f000:f024`
(two addresses or labels are separated by `:`).   To check what'll be printed try
`mem s_kernel_id`.  This shows 64 bytes by default but the same `/offset` or `:end`
trick works for custom ranges.

    PC f016  NV-BDIZC  A 00 X 00 Y 00 SP fd > mem s_kernel_id
    f030        54 61 6c 69 20 46  6f 72 74 68 20 32 20 64  |  Tali Forth 2 d|
    f040  65 66 61 75 6c 74 20 6b  65 72 6e 65 6c 20 66 6f  |efault kernel fo|
    f050  72 20 70 79 36 35 6d 6f  6e 20 28 30 34 2e 20 44  |r py65mon (04. D|
    f060  65 63 20 32 30 32 32 29  0a 00 00 00 00 00 00 00  |ec 2022)........|
    f070  00 00                                             |..              |

You can set breakpoints on read, write or execute.
Type `break xt_cold` to stop executing
once the startup message is printed.  We can also verify that
ROM (above $8000) is never written with `break 8000:ffff write`
but should also remove breakpoints on the magic IO block with `del f000/16`.
Let's also add a read breakpoint within the startup message: `break f040 read`.
This should should stop after reading the 'e' in 'default'.
Execution breakpoints are shown in disasembly listings as `B`,
and read/write breakpoints are highlighted in `memory` dumps.
With the above breakpoints, use `continue` to execute from the current PC:

    PC f030  NV-BDIZC  A 54 X 00 Y 00 SP fb > c
    Tali Forth 2 dBreak on reading $f040
    PC f01c  NV-BDIZC  A 65 X 0e Y 00 SP fd >

Continue again after the read breakpoint to land at `xt_cold`:

    efault kernel for py65mon (04. Dec 2022)
    PC 8000  NV-BDIZC  A 00 X 37 Y 00 SP fd > d
    xt_cold:
    code0:
    forth:
    *B 8000  d8        cld
       8001  a9 2d     lda  #$2d
       8003  85 12     sta  output
       ...

You can `step` instruction by instruction, or use `next` to step while treating
`jsr ... rts` as one step.  You can `call` a subroutine and return
to the current `pc` on completion, or just `run` from an arbitrary address or label.
Other useful commands are `fill` to change the contents of memory or `set`
to change a register or flag value.  Use `?` to see all available commands.

When the simulation is running past a breakpoint, type `ctrl-C` to return to the prompt.
Try `continue` again to enter the interactive Taliform REPL.  Put some numbers on the stack
and use `ctrl-C` to get back to the debugger.  Since Tali uses X as its stack pointer,
`mem 6e:80` will show our content:

    Tali Forth 2 for the 65c02
    Version 1.1 06. Apr 2024
    Copyright 2014-2024 Scot W. Stevenson, Sam Colwell, Patrick Surry
    Tali Forth 2 comes with absolutely NO WARRANTY
    Type 'bye' to exit
    1 2 3 4 .s <4> 1 2 3 4  ok     <ctrl-C>
    PC f02a  NV-BDIZC  A 03 X 6e Y 00 SP f9 > mem 6e:80
    0060                                             00 02  |              ..|
    0070  04 00 03 00 02 00 01 00  00 00 00 00 00 00 00 00  |................|

The extra `$2000` is presumably part of the REPL,
and the empty space beyond $78 is Tali's "flood plain".  Setting `break 78:80 any`
would be a good indicator of stack underflow.
When you're done `quit` will exit the debugger.  Hopefully you get the idea!

## Profiling

`c65` can optionally dump profiling data
to files called `c65-coverage.dat` and `c65-writes.dat`.
Each contains a binary dump of 64K `int`s, which respectively
count the number of accesses (read or write) to each memory
location, along with the number of writes.
(The number of reads can be inferred by differencing the arrays.)

These files are useful for profiling "hot spots" in simulated code,
and for ensuring that read-only regions are never written.
The `profile.ipynb` file contains a [jupyter notebook](https://jupyter.org/)
with some examples of exploring this data.

## Developers

`c65` should build on posix-like platforms such as OS X; windows native using [MinGW](https://en.wikipedia.org/wiki/MinGW); and on the Windows [WSL](https://learn.microsoft.com/en-us/windows/wsl/setup/environment).
The debugger uses line editing capabilities from a branch of [linenoise](https://github.com/msteveb/linenoise.git)
which supports both Windows and simple ANSI escapes for the prompt, with one modification to allow
SIGINT (ctrl-C) to interrupt the simulator rather than act as a line editing command.
[`fake65c02.h`](https://github.com/C-Chads/MyLittle6502) has been modified slightly to support extended W65C02
NOP instructions as well as disassembly.

Early on I tried a simulator based on https://github.com/omarandlorraine/fake6502 but it seems to have some subtle bug. It runs most of TaliForth in 65c02 mode but `: foo 3 2 + ;` fails with a stack underflow.
