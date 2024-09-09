These are basic tests of monitor functionality using a tiny wozmon image.

Rebuild wozmon.rom and wozmon.sym if required:

    64tass --nostart --vice-labels --labels=wozmon.sym --output wozmon.rom wozmon.asm

Run the tests like:

    ./c65 -r tests/wozmon.rom -l tests/wozmon.sym < tests/test.in | perl -pe 's/\x1b\[[0-9;]*[mG]//g' > tests/test.out
