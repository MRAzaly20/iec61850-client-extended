/* ============================================================
 *  sld_data.h — Single-Line Diagram data model
 *  GI 150kV — Equipment mapping to IEC 61850
 * ============================================================ */
#ifndef SLD_DATA_H
#define SLD_DATA_H

#include <cairo.h>
#include "substation_config.h"

/* ---- Equipment types ---- */
typedef enum {
    EQUIP_CB,           /* Circuit Breaker (XCBR) */
    EQUIP_DS_BUS,       /* Bus Disconnector (XSWI) */
    EQUIP_DS_LINE,      /* Line Disconnector (XSWI) */
    EQUIP_EARTH,        /* Earthing Switch (XSWI) */
    EQUIP_BUSBAR,       /* Busbar (non-interactive, decoration) */
    EQUIP_LABEL,        /* Text label (non-interactive) */
} EquipType;

/* ---- Dbpos states (IEC 61850) ---- */
#define DBPOS_INTERMEDIATE  0
#define DBPOS_OPEN          1
#define DBPOS_CLOSED        2
#define DBPOS_BAD_STATE     3

/* ---- Single equipment element on the SLD ---- */
typedef struct {
    char        ref[256];       /* ACSI path: e.g. "E01BCU/XCBR1.Pos.stVal" */
    char        doRef[256];     /* Control ref: e.g. "E01BCU/XCBR1.Pos" */
    char        label[64];      /* Display label */
    EquipType   type;
    int         state;          /* Dbpos value: 0-3 */
    double      x, y, w, h;    /* Bounding box for hit-test & drawing */
    int         bay_index;      /* Which bay this belongs to (0 to MAX_BAYS-1) */
} SldEquipment;

/* ---- Bay info (legacy structure preserved for compatibility) ---- */
typedef struct {
    const char *name;           /* "E01\nTRF-1" */
    const char *ied_prefix;     /* "E01BCU" */
    double      cx;             /* Center X of this bay column */
} SldBayInfo;

/* ---- Color helpers (Cyber-Tech Palette) ---- */
static inline void sld_state_color(int state, double *r, double *g, double *b) {
    switch (state) {
        case DBPOS_OPEN:   /* Green (Open - Outline only, no fill) */
            *r = 0.15; *g = 0.88; *b = 0.35; break;
        case DBPOS_CLOSED: /* Electric Cyan (Active / Closed - Filled) */
            *r = 0.0;  *g = 0.94; *b = 1.0;  break;
        default:           /* Deep Amber / Dark Yellow (Intermediate / Fault) */
            *r = 1.0;  *g = 0.72; *b = 0.0;  break;
    }
}

/* Default / compatibility count */
#define SLD_NUM_BAYS 6

/* Y coordinates for the double-bus bay topology:
 *   Bus A:       y=30
 *   Bus B:       y=70
 *   Bus A/B DS:  y=120
 *   CB:          y=190
 *   Line DS:     y=260
 *   Earth:       y=320
 *   Feeder:      y=380
 */
#define SLD_BUS1_Y       30.0
#define SLD_BUS2_Y       70.0
#define SLD_BUS_DS_Y    120.0
#define SLD_CB_Y        190.0
#define SLD_LINE_DS_Y   260.0
#define SLD_EARTH_Y     320.0
#define SLD_FEEDER_Y    380.0

#define SLD_DS_OFFSET    30.0   /* Horizontal branch span from center for the dual-bus disconnects */

#define SLD_EQUIP_W      36.0
#define SLD_EQUIP_H      36.0

#define SLD_CANVAS_W     1280.0
#define SLD_CANVAS_H      590.0

static inline void sld_get_center_offset(int widget_w, int widget_h, double *dx, double *dy) {
    *dx = (widget_w > SLD_CANVAS_W) ? (widget_w - SLD_CANVAS_W) / 2.0 : 0.0;
    *dy = (widget_h > SLD_CANVAS_H) ? (widget_h - SLD_CANVAS_H) / 2.0 : 0.0;
}


/* Maximum number of interactive equipment elements */
#define SLD_MAX_EQUIPMENT 128

/* ---- Init & Draw functions (implemented in sld_draw.c) ---- */
void sld_init_equipment(SldEquipment *equip, int *count, const SubstationConfig *cfg);
void sld_draw(cairo_t *cr, int width, int height,
              SldEquipment *equip, int count,
              const SubstationConfig *cfg,
              const int *bay_connected);
int  sld_hit_test(SldEquipment *equip, int count, double mx, double my, int widget_w, int widget_h);

#endif /* SLD_DATA_H */
