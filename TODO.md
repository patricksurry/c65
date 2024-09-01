TODO

- `continue <input>` would be useful to consume input and break on eof, e.g. for test coverage or
  batching an initial section of working code

- option for case insensitive labels on load or always?

- maybe restrict max dump / disasm size to 1K to avoid wraparound dump of almost all memory?

- disassemble backward from PC would be handy but seems indeterminate, e.g. bytes $20 $80 $60 before
  PC could be JSR $6080, BRA $60 or RTS.  can deduce where opcodes land if you have heatx data
