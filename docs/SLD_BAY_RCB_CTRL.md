## 3. FC / RCB / Control (V-FC, V-RCB, V-CTRL)

| ID | Trigger | UI message | Severity |
|---|---|---|---|
| V-FC-01 ST_UNREADABLE | readObject ST DATA_ACCESS_ERROR | E05 CB1: ST unreadable. Check FCDA XCBR1/Pos/stVal. | ERROR |
| V-FC-02 Q_BAD | q questionable | E05 CB1: Quality questionable — amber, control allowed. | WARN |
| V-RCB-01 RCB_ALIAS | RP.SwitchRCB01 missing but E05_BCU/E05BCU/LLN0.SwitchRCB exists | E05: RCB alias — using CID rptID. Reports OK. Suggest update. | WARN |
| V-RCB-02 RCB_MISSING | neither found | E05: No SwitchRCB. Fallback 1s poll, POLL ONLY badge. | WARN |
| V-CTRL-01 SBO | ctlModel 2 sbo (DPC1 default) | E05 CB1: SBO-normal — use Select and Operate. | INFO |
| V-CTRL-02 DIRECT | ctlModel 1 | E05 DS-A: Direct-normal — single Operate. | INFO |
| V-CTRL-03 STATUS_ONLY | ctlModel 0 | GGIO Ind1: status-only — control disabled, read-only. | WARN button disabled |
| V-CTRL-04 OPERATE_FAIL | operate FAILED / CommandTermination- | E05 CB1 FAILED — interlock? Check 86 / earth in server console. Keep dialog open. | ERROR |

## 4. Policy + acceptance

Validate: V-BAY parallel 500ms, then V-LN per enabled bay, then RCB/CTRL sample.
Apply blocked on any ERROR. WARN allows Apply anyway (degraded).
Reconnect only changed bay_index. Persist sld_bays.local.json (git-ignored).

Accept: E04+XSWI4 => V-LN-01 blocked grey. E01+XSWI4 default => V-LN-02.
E05 127.0.0.1:10205 XCBR1 => V-LN-07 + V-CTRL-01 green.
RCB RP.SwitchRCB01 => V-RCB-01 alias warning green reports.
