## 1. Bay Transport (V-BAY)

| ID | Trigger | UI message | Severity |
|---|---|---|---|
| V-BAY-01 EMPTY_HOST | host empty | E05: Host is empty. | ERROR CONFIG |
| V-BAY-02 BAD_IP | 10.0.5.999 | E05: not valid IPv4/hostname. | ERROR |
| V-BAY-03 BAD_PORT | 0/99999 | E05: Port out of range 1-65535. Use JSON port e.g. 10205. | ERROR |
| V-BAY-04 PORT_CLASH | E01+E02 both 10201 | E01/E02 duplicate 127.0.0.1:10201. See sas_server/config map. | ERROR |
| V-BAY-05 TIMEOUT | 10.0.5.1:102 vs JSON 127.0.0.1:10205 | E05: Timeout 500ms — server down or wrong IP (JSON says 127.0.0.1:10205). | ERROR COMM_DOWN |
| V-BAY-06 IED_MISMATCH | LD E05BCU missing, found E06BCU | E05: Connected but LD E05BCU not found. Wrong bay? | ERROR WRONG IED |
| V-BAY-07 DISABLED | enabled=false | E07: Disabled — layout only, no connect. | INFO grey |

## 2. Logical Node per apparatus (V-LN)

Refs: ref={ied}/{ld}/{ln}.Pos.stVal, doRef={ied}/{ld}/{ln}.Pos.

| ID | Trigger goose-v2.1 | UI message | Severity |
|---|---|---|---|
| V-LN-01 MISSING_ES_COUPLER | E04 XSWI4 ES | E04 ES (XSWI4): NOT IN MODEL — coupler E04BCU = XCBR1+XSWI1-2+GGIO only. Remove ES. | ERROR grey NOT IN MODEL |
| V-LN-02 MISSING_ES_TRF | E01 XSWI4 (current SLD default draws it, CID has none) | E01 ES (XSWI4): NOT IN MODEL — TRF E01-E03 have XSWI1-3 only. Read gives Object-Non-Existent. | ERROR — catches shipped default |
| V-LN-03 MISSING_LINEDS | E04 XSWI3 | E04 Line DS (XSWI3): NOT IN MODEL. | ERROR |
| V-LN-04 WRONG_INST | E06 CB1=XCBR2, only XCBR1 exists | E06 CB1 (XCBR2): NOT IN MODEL — only inst 1. Mean XCBR1? | ERROR |
| V-LN-05 WRONG_CLASS | CB1=XSWI1 | E05 CB1: XSWI cannot drive CB symbol. Use XCBRn. | ERROR |
| V-LN-06 DUP_LN | CB1+CB2 both XCBR1 | E05: XCBR1 bound twice. Unique inst per apparatus. | ERROR |
| V-LN-07 ST_OK | E05 XCBR1 Dbpos 2 | E05 CB1 (XCBR1): OK Dbpos=2 CLOSED q=good. | INFO cyan |
| V-LN-08 PREFIX | Q0XCBR1 vs CID prefix empty | E05: prefix Q0 not in CID. Clear or regen CID. | WARN/ERROR |
