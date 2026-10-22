/* ============================================================
 *  substation_config.h — Substation bay & apparatus configuration
 * ============================================================ */
#ifndef SUBSTATION_CONFIG_H
#define SUBSTATION_CONFIG_H

#define SLD_MAX_BAYS 16
#define SLD_MAX_EQUIP_PER_BAY 12
#define SLD_X_AUTO 9999.0

/* Generic HV apparatus entry (custom Logical Nodes). */
typedef struct {
    char key[32];
    char label[64];
    char ln[32];
    char do_name[32];
    char da_name[32];
    char fc_read[8];
    int  equip_type;
    double y_slot;
    double x_off;
} SldEquipConfig;

typedef struct {
    char name[64];           /* Display name: e.g. "E01\nTRF-1" */
    char ied_name[64];       /* "E01_BCU" */
    char ld_inst[64];        /* "E01BCU" */
    char host[64];           /* "127.0.0.1" */
    int  port;               /* 10201 */
    char rcb_ref[256];       /* "E01_BCUE01BCU/LLN0.RP.SwitchRCB01" */
    
    char ln_cb[32];          /* "XCBR1" */
    char ln_ds_a[32];        /* "XSWI1" */
    char ln_ds_b[32];        /* "XSWI2" */
    char ln_line_ds[32];     /* "XSWI3" */
    char ln_earth[32];       /* "XSWI4" */
    
    int  has_line_ds;        /* 1 if bay has line disconnector, 0 if coupler */
    int  has_earth;          /* 1 if bay has earth switch, 0 if coupler */
    double cx;               /* Center X on SLD, e.g. 120.0 */
    SldEquipConfig equipment[SLD_MAX_EQUIP_PER_BAY];
    int  num_equipment;
} SldBayConfig;

typedef struct {
    char substation_name[128];
    int num_bays;
    SldBayConfig bays[SLD_MAX_BAYS];
} SubstationConfig;

/* Initialize default 6-bay 150kV switchyard configuration */
void bay_sync_equipment_from_legacy(SldBayConfig *b);
void substation_config_init_default(SubstationConfig *cfg);

/* Load from JSON file (creates default file if not found or invalid) */
int  substation_config_load(const char *filepath, SubstationConfig *cfg);

/* Save to JSON file */
int  substation_config_save(const char *filepath, const SubstationConfig *cfg);

#endif /* SUBSTATION_CONFIG_H */
