TODO

- optional case insensitive labels

- feedback from "delete" or "delete xyz" on how many breakpoints were removed (did it do anything?)

- add "show" to summarize active breakpoints with ranges

- maybe restrict max dump / disasm size to 1K to avoid wraparound dump of almost all memory?

- disassemble backward from PC would be handy but seems indeterminate, e.g. bytes $20 $80 $60 before
  PC could be JSR $6080, BRA $60 or RTS

- labelfile is currently very specific to default c64tass format. It also tries to exclude numeric constants
  by only adding labels with an address starting with `$` followed by four digits.
  Perhaps vice label format (excluding constants) would be a better standard which would also work for `ca65`.

- needs command to dump the stats which are already collected.  this used to be a command-line option.

  FILE *fout;
  fout = fopen("c65-coverage.dat", "wb");
  fwrite(rws, sizeof(int), 65536, fout);
  fclose(fout);

  fout = fopen("c65-writes.dat", "wb");
  fwrite(writes, sizeof(int), 65536, fout);
  fclose(fout);

