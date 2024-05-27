TODO

- option for case insensitive labels on load or always?

- labelfile is currently very specific to default c64tass format. It also tries to exclude numeric constants
  by only adding labels with an address starting with `$` followed by four digits.
  Perhaps vice label format (excluding constants) would be a better standard which would also work for `ca65`.

- maybe restrict max dump / disasm size to 1K to avoid wraparound dump of almost all memory?

- disassemble backward from PC would be handy but seems indeterminate, e.g. bytes $20 $80 $60 before
  PC could be JSR $6080, BRA $60 or RTS

