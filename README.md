c65 provides a simple 65c02 simulator based on `fake65c02.h` from https://github.com/C-Chads/MyLittle6502

The simulator mimics py65mon by providing magic getc and putc memory
locations with the same default addresses.
It also provides a magic blockio memory address
which supports IO to an external binary file.

## Usage

The simplest usage is:

    c65 -r file.rom

This loads `file.rom` at the top of memory and resets to the address at $fffc,
corresonding directly to `py65mon -m 65c02 -r file.rom`.
Various options are supported to control the simulator, use `c65 -?` for up to date details.

    -a <address>    # load the rom file at a specific address
    -g <address>    # change the reset vector at $fffc
    -t <ticks>      # stop after ticks cycles (default forever)
    -i <address>    # change the magic getc address (default 0xf004)
    -o <address>    # change the magic putc putc address (default 0xf001)
    -x <address>    # change the block io base address (default 0xf010)
    -b <file>       # provide a block file to enable blockio

## Block IO

The base address (default $f010) is the first byte of a six byte interface:

    offset  name    I/O description
    0       action  I   initiate action (set other params first)
    1       status  O   returns 0 on success 0xff otherwise
    2       blknum  I   0-indexed block number to read or write
    4       bufptr  I   low-endian pointer to 1024 byte buffer to r/w

To initiate a block IO operation, set the `blknum` and `bufptr` parameters
and then write the `action` code to the base address. The `status`
value is returned. Four actions are currently supported:

- status (0): detect if blkio available, 0x0 if enabled, 0xff otherwise
- read (1): read the 1024 byte block @ blknum to bufptr
- write (2): write the 1024 byte block @ blknum from bufptr
- exit (ff): cleanly exit from the simulator, dumping profiling data

Note that an external blockfile must be specified with the `-b ...` option
to enable block IO. The file is simply a binary file with block k
mapped to offset k*1024 through (k+1)*1024-1.
With a two-byte `blknum` the maximum addressaable file size is 64Mb.

## Profiling

After a clean exit via the blockio hook, the simulator dumps
two files `c65-coverage.dat` and `c65-writes.dat`.
Each contains a binary dump of a 64K array of `int`s,
with the former counting the number of reads or writes to each memory
address in the simulator, and the latter counting the number of writes.
(The number of reads can be inferred by counting the number of reads.)

These files can be useful for profiling "hot spots" in simulated code,
and for ensuring that read-only regions are never written.
The `profile.ipynb` file contains a [jupyter notebook](https://jupyter.org/)
with some examples of exploring this data.

## Notes

Note I also tried a version based on https://github.com/omarandlorraine/fake6502 but it seems to have some subtle bug. It runs most of TaliForth in 65c02 mode but `: foo 3 2 + ;` fails with a stack underflow.
