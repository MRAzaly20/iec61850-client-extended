# Custom Logical Nodes — User Guide (goose-v2.1)

## What changed
- Each bay now has a generic `equipment[]` list (max 12).
  Each entry binds one SLD symbol to one LN.DO.
- Old JSON (ln_cb / ln_ds_a / ln_ds_b / ln_line_ds / ln_earth) still loads.
  On load it is converted to `equipment[]`. On save both are written.
- SLD renderer builds symbols from `equipment[]`, not fixed fields.
- Bay Config dialog has a Custom LN text area per bay.

## Custom LN text format (one per line)
KEY | Label | LN.DO | type=N
- N: 0=CB 1=DS_BUS 2=DS_LINE 3=EARTH
- LN examples: XCBR1 XCBR2 XSWI1 XSWI5 CSWI1 CSWI2 GGIO1
- DO default Pos. Use GGIO1.Ind1 for GGIO indications.

## Examples
CB1 | CB1 | XCBR1.Pos | type=0
CB2 | CB2 | XCBR2.Pos | type=0
DS-A | DS-A | XSWI1.Pos | type=1
DS-EXTRA | DS-X | XSWI5.Pos | type=1
ES2 | ES2 | XSWI4.Pos | type=3
IND | Ready | GGIO1.Ind1 | type=1

## Validation
- Empty LN is skipped.
- Unknown LN on server reads as Object-Non-Existent (see SLD_BAY_MSGS V-LN).
- Keep keys unique per bay.
- Save and Apply reconnects bays and rebuilds SLD symbols.
