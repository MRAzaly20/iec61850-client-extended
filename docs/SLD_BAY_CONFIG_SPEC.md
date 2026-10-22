# SLD Bay Config — Validation Messages Spec (goose-v2.1, DESIGN ONLY)

> Status: Proposal — no source changed. Ground truth from goose-v2.1 on 2026-10-18.
> Companion: `../config/sld_bays.example.json`, `../config/sld_topology.example.json`

## 0. Ground Truth

Client hardcoded: `src/main.c:13-17` host 127.0.0.1 port 10201+i;
`sld_data.h:59-66` LD E01BCU..E06BCU;
`sld_draw.c:41-87` add_equip ref={IED}/{LD}/{LN}.Pos.stVal, E04 (b==3) skips XSWI3/4;
`iec61850_conn.c:408` rcb E%02d_BCUE%02dBCU/LLN0.RP.SwitchRCB01.

Server: JSON ip:port is runtime truth (E01 127.0.0.1:10201, E04 :10204, E05 :10205).
CID doc IP 10.0.x.x only. RCB rptID in CID is E0X_BCU/E0XBCU/LLN0.SwitchRCB (no RP/01).

LN inventory: E01 LDevice E01BCU = XCBR1+CSWI1+XSWI1-3 (no XSWI4).
E04 E04BCU = XCBR1+CSWI1+XSWI1-2+GGIO Ind1 (no XSWI3/4).
E05 E05BCU = XCBR1+CSWI1+XSWI1-4 (full).
