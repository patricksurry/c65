c65 provides a simple 65c02 simulator based on `fake65c02.h` from https://github.com/C-Chads/MyLittle6502 which mimics py65mon
with the same default magic getc and putc memory locations.
It also provides a magic blockio memory address
which supports IO to an external binary file,
and collects simple memory access profiling data.

## Usage

The simplest usage is:

    c65 -r taliforth-py65mon.bin

This loads the TaliForth2 memory image at the top of memory
and resets to the address stored at $fffc. This corresponds exactly to
`py65mon -m 65c02 -r taliforth-py65mon.bin` without the monitor tools.
Various options control the simulator (use `c65 -?` for up-to-date details):

    -a <address>    # load the rom file at a specific address
    -g <address>    # set the reset vector at $fffc
    -t <ticks>      # stop after ticks cycles (default forever)
    -m <address>    # change the magic IO base address (default $f000)
    -b <file>       # provide a block file to enable blockio

## Magic IO

c65 provides a magic IO block that spans a 22 byte range
and is normally based at $f000. Use `-m` to change the base address.
This supports a number of IO functions:

    Addr    Name    Description

    $f001   putc    Writing here sends the byte to stdout
    $f004   getc    Reading here blocks on an byte from stdin
    $f005   peekc   Non-blocking read, bit7=1 if ready with 7bit character

    $f006   start   Reading here starts the cycle counter
    $f007   stop    Reading here stops the cycle counter
    $f008-b cycles  Current 32 bit cycle count in NUXI order

    $f010   blkio   Write here to initiate a block IO action (see below)
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

- status (0): check blkio status returning 0x0 if enabled, 0xff otherwise
- read (1): read the 1024 byte block @ blknum to bufptr
- write (2): write the 1024 byte block @ blknum from bufptr

Note that an external blockfile must be specified with the `-b ...` option
to enable block IO. The file is simply a binary file with block k
mapped to offset k*1024 through (k+1)*1024-1.
The two-byte `blknum` supports a maximum addressable file size of 64Mb.
A portable (cross-platform) check for blkio is writing 1 to `status`,
then writing 0 to `action` and finally checking if `status` is 0.

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

## Profiling

After exiting, the simulator dumps
two files called `c65-coverage.dat` and `c65-writes.dat`.
Each contains a binary dump of 64K `int`s, which respectively
count the number of accesses (read or write) to each memory
location, along with the number of writes.
(The number of reads can be inferred by differencing the arrays.)

These files are useful for profiling "hot spots" in simulated code,
and for ensuring that read-only regions are never written.
The `profile.ipynb` file contains a [jupyter notebook](https://jupyter.org/)
with some examples of exploring this data.

## Notes

Note I also tried a version based on https://github.com/omarandlorraine/fake6502 but it seems to have some subtle bug. It runs most of TaliForth in 65c02 mode but `: foo 3 2 + ;` fails with a stack underflow.

Windows setup

Set up WSL as explained at:

https://learn.microsoft.com/en-us/windows/wsl/setup/environment

Start ubuntu and install build tools:

ubuntu
sudo apt-get install build-essential

Change to the folder where you cloned taliforth, e.g. in the windows file system. Then build c65 and run taliforth binary:

cd /mnt/c/Users/patri/tali
cd c65
make
cd ..
c65/c65 -r taliforth-py65mon.bin
