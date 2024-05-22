These are basic tests of monitor functionality using a tiny wozmon image.

Build wozmon.rom and wozmon.sym with:

    64tass --nostart --labels=wozmon.sym --output wozmon.rom wozmon.asm

Run the tests like:

    ./c65 -r wozmon.rom -l wozmon.sym < test.in | perl -pe 's/\x1b\[[0-9;]*[mG]//g' > test.out
