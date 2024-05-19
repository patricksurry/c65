TODO

- Windows WSL and mac OS work OK, but windows native fails on termio - check whether there's an easy workaround.

- labelfile is very specific to default c64tass format, and tries to exclude numeric constants.
  maybe vice label format excluding constants is a better baseline since that also works for c65 etc.

- the monitor currently keeps the last label for any given address.  probably better to allow
  multiple labels per address (e.g. z_number, xt_editor_wordlist, xt_one).  disasm could display
  all of them at their address, but would still need to pick one for references

- add command to dump stats which are already collected.  this used to be a command-line option.

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
