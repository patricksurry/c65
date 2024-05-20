TODO

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

- could add command to show current cycles, and/or set/reset a cycle limit.  this used to be a command-line option:

  long max_ticks = -1;
    case 't':
      max_ticks = strtol(optarg, NULL, 0);
      break;
