These are basic tests of monitor functionality using a tiny wozmon image.

Rebuild wozmon.rom and wozmon.sym if required:

    64tass --nostart --vice-labels --labels=wozmon.sym --output wozmon.rom wozmon.asm

Run the tests like:

    ./c65 -r tests/wozmon.rom -l tests/wozmon.sym < tests/test.in | perl -pe 's/\x1b\[[0-9;]*[mG]//g' > tests/test.out

We can also run Klaus Dormann functional tests https://github.com/Klaus2m5/6502_65C02_functional_tests or a ca65-assembler version e.g. https://github.com/amb5l/6502_65C02_functional_tests

Download a pre-compiled binary image, continue past a couple of BRK's 
and then interrupt to see where it gets trapped.  
Or set a breakpoint at the expected trap location.  
Both test sets use <16Kb of data plus reset vectors, with the empty space filled with $ff.
For example with the 6502 functional tests 0x3469 is success (see https://github.com/Klaus2m5/6502_65C02_functional_tests/blob/master/bin_files/6502_functional_test.lst).

Expected cycle counts:
- 6502  (to break at 3469) ticks=96,561,369 (about 1s @ 100MHz)
- 65c02 (to break at 24f1) ticks=66,894,624

TODO: I suspect that the V-flag in decimal mode would not pass the Clark's test.

```sh
> ./c65 -r tests/6502_functional_test.bin -s 0x400 -gg
c65: reading 6502_functional_test.bin to $0000:$ffff
c65: PC=0400 A=00 X=00 Y=00 S=fd FLAGS=<N0 V0 B0 D0 I1 Z0 C0> ticks=0
Type ? for help, ctrl-C to interrupt, quit to exit.
*  37ab  08          php  
PC 37ab  nv-bdIzc  A 42 X 52 Y 4b SP fc > c
*  37ab  08          php  
PC 37ab  NV-BdIZC  A bd X ad Y b4 SP fc > c
*  3469  4c 69 34    jmp  $3469
PC 3469  NV-BdizC  A f0 X 0e Y ff SP ff > q
c65: PC=3469 A=f0 X=0e Y=ff S=ff FLAGS=<N1 V1 B1 D0 I0 Z0 C1> ticks=957765558
```

Similarly with the extended tests, where 24f1 indicates success.

```sh
> ./c65 -r tests/65C02_extended_opcodes_test.bin -s 0x400 -gg
c65: reading tests/65C02_extended_opcodes_test.bin to $0000:$ffff
c65: PC=0400 A=00 X=00 Y=00 S=fd FLAGS=<N0 V0 B0 D0 I1 Z0 C0> ticks=0
Type ? for help, ctrl-C to interrupt, quit to exit.
*  2724  08          php  
PC 2724  nv-bdIzc  A 42 X 52 Y 4b SP fc > c
*  2724  08          php  
PC 2724  NV-BdIZC  A bd X ad Y b4 SP fc > c
*  24f1  4c f1 24    jmp  $24f1
PC 24f1  NV-BdizC  A f0 X ff Y ff SP ff > q
c65: PC=24f1 A=f0 X=ff Y=ff S=ff FLAGS=<N1 V1 B1 D0 I0 Z0 C1> ticks=389176929
```



