/* ============================================================
 *  sld_draw.c — Single-Line Diagram Cairo renderer
 *  GI 150kV — Dynamic SLD with interactive equipment
 * ============================================================ */
#include "sld_data.h"
#include <string.h>
#include <stdio.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <pango/pangocairo.h>

/* ============================================================
 *  Text rendering helper using Pango (GTK+ safe on Windows)
 * ============================================================ */
static void draw_text_pango(cairo_t *cr, const char *text, const char *font, double x, double y, gboolean center_x, gboolean center_y) {
    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_text(layout, text, -1);
    
    PangoFontDescription *desc = pango_font_description_from_string(font);
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    
    int w, h;
    pango_layout_get_pixel_size(layout, &w, &h);
    
    double dx = x;
    double dy = y;
    if (center_x) dx -= w / 2.0;
    if (center_y) dy -= h / 2.0;
    
    cairo_move_to(cr, dx, dy);
    pango_cairo_update_layout(cr, layout);
    pango_cairo_show_layout(cr, layout);
    
    g_object_unref(layout);
}

/* ============================================================
 *  Equipment initialization — builds the full bay layout
 * ============================================================ */
static void add_equip_cfg(SldEquipment *eq, int *n, int bay,
                      const char *prefix, const SldEquipConfig *c,
                      EquipType type, double cx, double y)
{
    SldEquipment *e = &eq[*n];
    const char *ln = (c && c->ln[0]) ? c->ln : "?";
    char clean_dot[64];
    snprintf(clean_dot, sizeof(clean_dot), "%s", (c && c->do_name[0]) ? c->do_name : "Pos");
    char *p_pipe = strchr(clean_dot, '|');
    if (p_pipe) *p_pipe = '\0';
    char *p_sp = strchr(clean_dot, ' ');
    if (p_sp) *p_sp = '\0';
    if (!clean_dot[0]) strcpy(clean_dot, "Pos");
    const char *dat = (c && c->da_name[0]) ? c->da_name : "stVal";
    snprintf(e->ref, sizeof(e->ref), "%s/%s.%s.%s", prefix, ln, clean_dot, dat);
    snprintf(e->doRef, sizeof(e->doRef), "%s/%s.%s", prefix, ln, clean_dot);
    snprintf(e->label, sizeof(e->label), "%s",
        (c && c->label[0]) ? c->label : ln);
    e->type = type;
    e->state = DBPOS_INTERMEDIATE;
    e->x = cx - SLD_EQUIP_W / 2;
    e->y = y - SLD_EQUIP_H / 2;
    e->w = SLD_EQUIP_W;
    e->h = SLD_EQUIP_H;
    e->bay_index = bay;
    (*n)++;
}

static void add_equip(SldEquipment *eq, int *n, int bay,
                      const char *prefix, const char *ln, const char *label,
                      EquipType type, double cx, double y)
{
    SldEquipment *e = &eq[*n];
    snprintf(e->ref, sizeof(e->ref), "%s/%s.Pos.stVal", prefix, ln);
    snprintf(e->doRef, sizeof(e->doRef), "%s/%s.Pos", prefix, ln);
    snprintf(e->label, sizeof(e->label), "%s", label);
    e->type = type;
    e->state = DBPOS_INTERMEDIATE;
    e->x = cx - SLD_EQUIP_W / 2;
    e->y = y  - SLD_EQUIP_H / 2;
    e->w = SLD_EQUIP_W;
    e->h = SLD_EQUIP_H;
    e->bay_index = bay;
    (*n)++;
}

void sld_init_equipment(SldEquipment *equip, int *count, const SubstationConfig *cfg) {
    *count = 0;
    double es_offset = 45.0;
    double b_off = SLD_DS_OFFSET;
    int num = cfg ? cfg->num_bays : 0;

    for (int b = 0; b < num; b++) {
        const SldBayConfig *bay = &cfg->bays[b];
        double cx = bay->cx;
        char prefix[128];
        if (bay->ld_inst[0] != '\0') {
            snprintf(prefix, sizeof(prefix), "%s%s", bay->ied_name, bay->ld_inst);
        } else {
            snprintf(prefix, sizeof(prefix), "%s", bay->ied_name);
        }

        /* Generic equipment list: custom Logical Nodes per bay. */
        for (int k = 0; k < bay->num_equipment; k++) {
            const SldEquipConfig *c = &bay->equipment[k];
            if (!c->ln[0]) continue;
            EquipType t = EQUIP_DS_BUS;
            double yy = SLD_BUS_DS_Y;
            double xx = cx;
            if (c->equip_type == 0) { t = EQUIP_CB; yy = SLD_CB_Y; }
            else if (c->equip_type == 1) { t = EQUIP_DS_BUS; yy = SLD_BUS_DS_Y; }
            else if (c->equip_type == 2) { t = EQUIP_DS_LINE; yy = SLD_LINE_DS_Y; }
            else if (c->equip_type == 3) { t = EQUIP_EARTH; yy = SLD_EARTH_Y; }
            if (c->y_slot > 1.0) yy = c->y_slot;
            if (c->x_off != SLD_X_AUTO) {
                xx = cx + c->x_off;
            } else if (t == EQUIP_DS_BUS) {
                xx = cx + ((k % 2 == 0) ? -b_off : b_off);
            } else if (t == EQUIP_EARTH) {
                xx = cx + es_offset;
            }
            add_equip_cfg(equip, count, b, prefix, c, t, xx, yy);
        }
    }
}

/* ============================================================
 *  Hit-testing — returns equipment index or -1
 * ============================================================ */
int sld_hit_test(SldEquipment *equip, int count, double mx, double my, int widget_w, int widget_h) {
    /* Inverse of the centering transform applied in sld_draw */
    double dx, dy;
    sld_get_center_offset(widget_w, widget_h, &dx, &dy);
    double lx = mx - dx;
    double ly = my - dy;

    double pad = 6.0;  /* 6px click padding around switchgear */
    for (int i = 0; i < count; i++) {
        SldEquipment *e = &equip[i];
        if (e->type == EQUIP_BUSBAR || e->type == EQUIP_LABEL) continue;
        if (lx >= (e->x - pad) && lx <= (e->x + e->w + pad) &&
            ly >= (e->y - pad) && ly <= (e->y + e->h + pad))
            return i;
    }
    return -1;
}

/* ============================================================
 *  Cairo drawing primitives
 * ============================================================ */

static void draw_busbar(cairo_t *cr, double x1, double x2, double y) {
    cairo_set_source_rgb(cr, 0.9, 0.4, 0.1);  /* Orange busbar */
    cairo_set_line_width(cr, 4.0);
    cairo_move_to(cr, x1, y);
    cairo_line_to(cr, x2, y);
    cairo_stroke(cr);
}

/* Draw a line with a subtle 'bloom' glow effect */
static void draw_neon_line(cairo_t *cr, double x1, double y1, double x2, double y2, double r, double g, double b, double w) {
    /* Outer glow */
    cairo_set_source_rgba(cr, r, g, b, 0.15);
    cairo_set_line_width(cr, w * 3.0);
    cairo_move_to(cr, x1, y1); cairo_line_to(cr, x2, y2); cairo_stroke(cr);
    
    /* Inner glow */
    cairo_set_source_rgba(cr, r, g, b, 0.4);
    cairo_set_line_width(cr, w * 1.5);
    cairo_move_to(cr, x1, y1); cairo_line_to(cr, x2, y2); cairo_stroke(cr);

    /* Bright core */
    cairo_set_source_rgba(cr, r + 0.2, g + 0.2, b + 0.2, 1.0);
    cairo_set_line_width(cr, w);
    cairo_move_to(cr, x1, y1); cairo_line_to(cr, x2, y2); cairo_stroke(cr);
}

static void draw_line(cairo_t *cr, double x1, double y1, double x2, double y2) {
    /* Feeder lines - thinner and muted */
    cairo_set_source_rgb(cr, 0.31, 0.33, 0.37);
    cairo_set_line_width(cr, 1.5);
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);
}

static void draw_feeder_line(cairo_t *cr, double cx, double y1, double y2) {
    draw_line(cr, cx, y1, cx, y2);
}

static void draw_bay_line(cairo_t *cr, double x1, double y1, double x2, double y2, gboolean active) {
    if (active) {
        /* A deeper/darker neon blue for active bays */
        draw_neon_line(cr, x1, y1, x2, y2, 0.0, 0.6, 0.9, 1.5);
    } else {
        draw_line(cr, x1, y1, x2, y2);
    }
}

static void draw_bay_feeder(cairo_t *cr, double cx, double y1, double y2, gboolean active) {
    draw_bay_line(cr, cx, y1, cx, y2, active);
}

/* Circuit breaker: Cyber-Tech hollow/filled box */
static void draw_cb(cairo_t *cr, SldEquipment *e) {
    double r, g, b;
    sld_state_color(e->state, &r, &g, &b);
    double cx = e->x + e->w / 2;
    double cy = e->y + e->h / 2;
    double s = e->w * 0.4;

    /* Hollow frame */
    cairo_set_source_rgb(cr, r, g, b);
    cairo_set_line_width(cr, 2.0);
    cairo_rectangle(cr, cx - s, cy - s, s * 2, s * 2);
    
    if (e->state == DBPOS_CLOSED) {
        cairo_fill_preserve(cr);
        /* Inner glowing effect for active CB */
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.4);
        cairo_rectangle(cr, cx - s + 2, cy - s + 2, s * 2 - 4, s * 2 - 4);
        cairo_fill(cr);
    } else {
        cairo_stroke(cr);
    }
}

/* Disconnector: two angled lines (knife switch symbol) */
static void draw_ds(cairo_t *cr, SldEquipment *e) {
    double r, g, b;
    sld_state_color(e->state, &r, &g, &b);
    double cx = e->x + e->w / 2;
    double cy = e->y + e->h / 2;
    double s = e->w * 0.35;

    cairo_set_source_rgb(cr, r, g, b);
    cairo_set_line_width(cr, 3.0);

    if (e->state == DBPOS_CLOSED) {
        /* Closed: vertical line */
        cairo_move_to(cr, cx, cy - s);
        cairo_line_to(cr, cx, cy + s);
    } else {
        /* Open: blade swung widely open to the left */
        cairo_move_to(cr, cx, cy + s);
        cairo_line_to(cr, cx - s * 1.4, cy - s * 0.2);
    }
    cairo_stroke(cr);

    /* Contact points */
    cairo_arc(cr, cx, cy - s, 3, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_arc(cr, cx, cy + s, 3, 0, 2 * M_PI);
    cairo_fill(cr);
}

/* Earth switch: horizontal DS blade branching off feeder, ground symbol at tip */
static void draw_earth(cairo_t *cr, SldEquipment *e) {
    double r, g, b;
    sld_state_color(e->state, &r, &g, &b);
    double cx = e->x + e->w / 2;
    double cy = e->y + e->h / 2;
    double s = e->w * 0.35;

    /* Horizontal connection line from feeder to ES switch */
    cairo_set_source_rgb(cr, 0.6, 0.6, 0.65);
    cairo_set_line_width(cr, 2.0);
    
    /* ES is offset by 45 pixels from the main vertical feeder */
    cairo_move_to(cr, cx - 45.0, cy);
    cairo_line_to(cr, cx - s, cy);
    cairo_stroke(cr);

    /* DS blade (horizontal) */
    cairo_set_source_rgb(cr, r, g, b);
    cairo_set_line_width(cr, 3.0);

    /* Left contact point */
    cairo_arc(cr, cx - s, cy, 3, 0, 2 * M_PI);
    cairo_fill(cr);

    if (e->state == DBPOS_CLOSED) {
        /* Closed: glowing line to right contact */
        draw_neon_line(cr, cx - s, cy, cx + s, cy, r, g, b, 3.0);
    } else {
        /* Open: angled blade upward */
        cairo_set_source_rgb(cr, r, g, b);
        cairo_set_line_width(cr, 2.5);
        cairo_move_to(cr, cx - s, cy);
        cairo_line_to(cr, cx + s * 0.3, cy - s * 1.2);
        cairo_stroke(cr);
    }

    /* Right contact point */
    cairo_set_source_rgb(cr, r, g, b);
    cairo_arc(cr, cx + s, cy, 3, 0, 2 * M_PI);
    cairo_fill(cr);

    /* Ground symbol path: horizontal line from contact, then vertical line down */
    double h_len = e->w * 1.5;
    double v_len = 15.0;
    double gx = cx + s + h_len;
    double gy = cy + v_len;

    cairo_set_source_rgb(cr, r, g, b);
    cairo_set_line_width(cr, 2.0);

    /* Draw the L-shaped connector (horizontal then vertical) */
    cairo_move_to(cr, cx + s, cy);
    cairo_line_to(cr, gx, cy);
    cairo_line_to(cr, gx, gy);
    cairo_stroke(cr);

    /* Ground lines (3 horizontal, decreasing width) */
    for (int i = 0; i < 3; i++) {
        double w_h = s * (1.0 - i * 0.3);
        double yy = gy + i * 4;
        cairo_move_to(cr, gx - w_h, yy);
        cairo_line_to(cr, gx + w_h, yy);
        cairo_stroke(cr);
    }
}

/* ============================================================
 *  Additional UI Symbols
 * ============================================================ */

/* Draw an ANSI 3-winding transformer symbol */
static void draw_ansi_transformer(cairo_t *cr, double cx, double y) {
    cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
    cairo_set_line_width(cr, 1.5);

    double r = 16.0;  /* Circle radius */
    double top_x  = cx,         top_y  = y + r;
    double bl_x   = cx - r,     bl_y   = y + r + r * 1.4;
    double br_x   = cx + r,     br_y   = y + r + r * 1.4;

    /* Top lead line from feeder to top circle */
    cairo_move_to(cr, cx, y);
    cairo_line_to(cr, cx, top_y - r);
    cairo_stroke(cr);

    /* Draw three circles */
    cairo_arc(cr, top_x, top_y, r, 0, 2 * M_PI);
    cairo_stroke(cr);
    cairo_arc(cr, bl_x, bl_y, r, 0, 2 * M_PI);
    cairo_stroke(cr);
    cairo_arc(cr, br_x, br_y, r, 0, 2 * M_PI);
    cairo_stroke(cr);
}

/* Draw a downward arrow for line bay feeds */
static void draw_downward_arrow(cairo_t *cr, double cx, double y) {
    cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
    cairo_set_line_width(cr, 2.0);
    
    cairo_move_to(cr, cx, y);
    cairo_line_to(cr, cx, y + 25);
    
    /* Arrow head */
    cairo_move_to(cr, cx - 6, y + 17);
    cairo_line_to(cr, cx, y + 25);
    cairo_line_to(cr, cx + 6, y + 17);
    
    cairo_stroke(cr);
}

static void draw_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2.0, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2.0);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2.0, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3.0 * M_PI / 2.0);
    cairo_close_path(cr);
}

/* Bay communication indicator badge */
static void draw_bay_connection_indicator(cairo_t *cr, double cx, double y, const char *host, int port, gboolean connected) {
    (void)host;
    double pill_w = 100.0;
    double pill_h = 20.0;
    double pill_x = cx - pill_w / 2.0;
    double pill_y = y;

    /* Background pill */
    draw_rounded_rect(cr, pill_x, pill_y, pill_w, pill_h, 10.0);
    if (connected) {
        cairo_set_source_rgba(cr, 0.04, 0.20, 0.12, 0.90);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.13, 0.85, 0.45, 0.50);
        cairo_set_line_width(cr, 1.2);
        cairo_stroke(cr);
    } else {
        cairo_set_source_rgba(cr, 0.22, 0.06, 0.07, 0.90);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.85, 0.25, 0.25, 0.40);
        cairo_set_line_width(cr, 1.2);
        cairo_stroke(cr);
    }

    /* Status LED Dot */
    double dot_x = pill_x + 14.0;
    double dot_y = pill_y + pill_h / 2.0;

    if (connected) {
        /* Outer bloom */
        cairo_set_source_rgba(cr, 0.0, 0.94, 0.5, 0.35);
        cairo_arc(cr, dot_x, dot_y, 6.0, 0, 2 * M_PI);
        cairo_fill(cr);
        /* Inner glowing core */
        cairo_set_source_rgb(cr, 0.1, 0.98, 0.55);
        cairo_arc(cr, dot_x, dot_y, 3.5, 0, 2 * M_PI);
        cairo_fill(cr);
        /* Text label */
        cairo_set_source_rgb(cr, 0.35, 0.95, 0.65);
        draw_text_pango(cr, "COMM OK", "Inter Bold 9", dot_x + 8.0, dot_y, FALSE, TRUE);
    } else {
        /* Outer bloom */
        cairo_set_source_rgba(cr, 0.9, 0.2, 0.2, 0.30);
        cairo_arc(cr, dot_x, dot_y, 5.5, 0, 2 * M_PI);
        cairo_fill(cr);
        /* Inner core */
        cairo_set_source_rgb(cr, 0.95, 0.3, 0.3);
        cairo_arc(cr, dot_x, dot_y, 3.5, 0, 2 * M_PI);
        cairo_fill(cr);
        /* Text label */
        cairo_set_source_rgb(cr, 0.85, 0.45, 0.45);
        draw_text_pango(cr, "OFFLINE", "Inter Bold 9", dot_x + 8.0, dot_y, FALSE, TRUE);
    }

    /* Target port caption right below the pill */
    char port_info[32];
    snprintf(port_info, sizeof(port_info), "Port %d", port);
    cairo_set_source_rgb(cr, 0.50, 0.55, 0.60);
    draw_text_pango(cr, port_info, "Roboto Medium 8", cx, pill_y + pill_h + 8.0, TRUE, FALSE);
}

/* Bay label with server connection indicator */
static void draw_bay_label(cairo_t *cr, const SldBayConfig *bay, double cx, double offset_y, gboolean connected) {
    cairo_set_source_rgb(cr, 0.75, 0.75, 0.8);

    /* Split by newline */
    char buf[64];
    strncpy(buf, bay && bay->name ? bay->name : "", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *line1 = buf;
    char *line2 = strchr(buf, '\n');
    if (line2) { *line2 = '\0'; line2++; }

    draw_text_pango(cr, line1, "Roboto Bold 12", cx, SLD_FEEDER_Y + 13 + offset_y, TRUE, FALSE);

    if (line2) {
        cairo_set_source_rgb(cr, 0.45, 0.5, 0.55);
        draw_text_pango(cr, line2, "Inter Regular 10", cx, SLD_FEEDER_Y + 35 + offset_y, TRUE, FALSE);
    }

    /* Connection indicator badge - positioned lower with generous breathing room */
    double ind_y = SLD_FEEDER_Y + 75 + offset_y;
    draw_bay_connection_indicator(cr, cx, ind_y,
                                  bay ? bay->host : "127.0.0.1",
                                  bay ? bay->port : 0,
                                  connected);
}

/* Equipment label */
static void draw_equip_label(cairo_t *cr, SldEquipment *e) {
    cairo_set_source_rgb(cr, 0.65, 0.65, 0.7);
    
    int w, h;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_text(layout, e->label, -1);
    PangoFontDescription *desc = pango_font_description_from_string("Roboto Medium 9");
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    
    pango_layout_get_pixel_size(layout, &w, &h);
    
    double lx, ly;
    if (e->type == EQUIP_DS_BUS) {
        if (strstr(e->label, "DS-A")) {
            lx = e->x - w - 8.0;
        } else {
            lx = e->x + e->w + 2.0;
        }
        ly = e->y + e->h / 2 - h / 2.0;
    } else if (e->type == EQUIP_EARTH) {
        lx = e->x + e->w / 2 - w / 2.0 - 5.0;
        ly = e->y + e->h + 0.0;
    } else {
        lx = e->x - w - 8.0;
        ly = e->y + e->h / 2 - h / 2.0;
        if (e->type == EQUIP_CB && strstr(e->label, "CB") && e->bay_index == 3) {
            lx = e->x - w - 20.0;
        }
    }
    
    cairo_move_to(cr, lx, ly);
    pango_cairo_update_layout(cr, layout);
    pango_cairo_show_layout(cr, layout);
    
    g_object_unref(layout);
}

/* ============================================================
 *  Main SLD drawing function
 * ============================================================ */
void sld_draw(cairo_t *cr, int width, int height,
              SldEquipment *equip, int count,
              const SubstationConfig *cfg,
              const int *bay_connected)
{
    (void)width; (void)height;

    /* Background - Deep Charcoal / Midnight Blue */
    cairo_set_source_rgb(cr, 0.043, 0.055, 0.078);
    cairo_paint(cr);

    /* Title */
    cairo_set_source_rgb(cr, 0.85, 0.85, 0.9);
    draw_text_pango(cr, cfg ? cfg->substation_name : "Single Line Diagram", "Inter Bold 15", 20, 20, FALSE, FALSE);

    int num_bays = cfg ? cfg->num_bays : 0;

    /* Overall Bay Comms Status Badge */
    int online_bays = 0;
    for (int i = 0; i < num_bays; i++) {
        if (bay_connected && bay_connected[i]) online_bays++;
    }
    char status_badge[64];
    snprintf(status_badge, sizeof(status_badge), "BAYS ONLINE: %d / %d", online_bays, num_bays);
    if (online_bays == num_bays && num_bays > 0) {
        cairo_set_source_rgb(cr, 0.1, 0.95, 0.55);
    } else if (online_bays > 0) {
        cairo_set_source_rgb(cr, 1.0, 0.75, 0.2);
    } else {
        cairo_set_source_rgb(cr, 0.95, 0.35, 0.35);
    }
    draw_text_pango(cr, status_badge, "Inter Bold 10", 280, 23, FALSE, FALSE);

    /* Center the SLD on-screen with saved transformation state */
    double dx, dy;
    sld_get_center_offset(width, height, &dx, &dy);
    cairo_save(cr);
    cairo_translate(cr, dx, dy);

    double bus_x_start = 50.0;
    double bus_x_end = (num_bays > 0) ? (cfg->bays[num_bays - 1].cx + 80.0) : 1200.0;
    if (bus_x_end < 1200.0) bus_x_end = 1200.0;

    /* Cyber-Tech gradient busbars with neon bloom */
    draw_neon_line(cr, bus_x_start, SLD_BUS1_Y, bus_x_end, SLD_BUS1_Y, 0.0, 0.94, 1.0, 4.0);
    draw_neon_line(cr, bus_x_start, SLD_BUS2_Y, bus_x_end, SLD_BUS2_Y, 0.0, 0.94, 1.0, 4.0);
    
    /* Bus labels */
    cairo_set_source_rgb(cr, 0.9, 0.4, 0.1);
    draw_text_pango(cr, "BUS A", "Roboto Bold 10", bus_x_start - 55, SLD_BUS1_Y, FALSE, TRUE);
    draw_text_pango(cr, "BUS B", "Roboto Bold 10", bus_x_start - 55, SLD_BUS2_Y, FALSE, TRUE);

    /* Draw feeder lines and equipment for each bay */
    for (int b = 0; b < num_bays; b++) {
        const SldBayConfig *bay = &cfg->bays[b];
        double cx = bay->cx;

        double ds_s = SLD_EQUIP_W * 0.35;
        double cb_s = SLD_EQUIP_W * 0.4;
        double b_off = SLD_DS_OFFSET;

        /* Determine bay active status */
        gboolean ds_a_closed = FALSE;
        gboolean ds_b_closed = FALSE;
        gboolean cb_closed = FALSE;
        gboolean line_ds_closed = FALSE;

        for (int i = 0; i < count; i++) {
            if (equip[i].bay_index == b) {
                if (equip[i].type == EQUIP_DS_BUS) {
                    if (strstr(equip[i].label, "DS-A") && equip[i].state == DBPOS_CLOSED) ds_a_closed = TRUE;
                    if (strstr(equip[i].label, "DS-B") && equip[i].state == DBPOS_CLOSED) ds_b_closed = TRUE;
                }
                if (equip[i].type == EQUIP_CB && equip[i].state == DBPOS_CLOSED) cb_closed = TRUE;
                if (equip[i].type == EQUIP_DS_LINE && equip[i].state == DBPOS_CLOSED) line_ds_closed = TRUE;
            }
        }

        if (!bay->has_line_ds && !bay->has_earth) {
            /* Coupler — parallel horizontal branches to CB */
            double h_y = SLD_CB_Y;

            draw_neon_line(cr, cx - b_off, SLD_BUS1_Y, cx - b_off, SLD_BUS_DS_Y - ds_s, 0.0, 0.94, 1.0, 1.5);
            draw_bay_feeder(cr, cx - b_off, SLD_BUS_DS_Y + ds_s, h_y, ds_a_closed);
            draw_bay_line(cr, cx - b_off, h_y, cx - cb_s, h_y, ds_a_closed);

            draw_neon_line(cr, cx + b_off, SLD_BUS2_Y, cx + b_off, SLD_BUS_DS_Y - ds_s, 0.0, 0.94, 1.0, 1.5);
            draw_bay_feeder(cr, cx + b_off, SLD_BUS_DS_Y + ds_s, h_y, ds_b_closed);
            draw_bay_line(cr, cx + cb_s, h_y, cx + b_off, h_y, ds_b_closed);

            gboolean is_conn = (bay_connected && bay_connected[b]);
            draw_bay_label(cr, bay, cx, 60.0, is_conn);
            continue;
        }

        gboolean pre_cb_active = (ds_a_closed || ds_b_closed);
        gboolean post_cb_active = (pre_cb_active && cb_closed);
        gboolean post_line_active = (post_cb_active && line_ds_closed);

        /* Normal Bays: Left branch Bus A DS */
        draw_neon_line(cr, cx - b_off, SLD_BUS1_Y, cx - b_off, SLD_BUS_DS_Y - ds_s, 0.0, 0.94, 1.0, 1.5);
        /* Right branch Bus B DS */
        draw_neon_line(cr, cx + b_off, SLD_BUS2_Y, cx + b_off, SLD_BUS_DS_Y - ds_s, 0.0, 0.94, 1.0, 1.5);
        
        /* Vertical stubs down from the bus disconnectors to the tie line */
        double tie_y = SLD_BUS_DS_Y + ds_s + 30.0;
        draw_bay_feeder(cr, cx - b_off, SLD_BUS_DS_Y + ds_s, tie_y, ds_a_closed);
        draw_bay_feeder(cr, cx + b_off, SLD_BUS_DS_Y + ds_s, tie_y, ds_b_closed);

        /* Horizontal tie connecting the bottom of both Bus A and Bus B Disconnectors */
        draw_bay_line(cr, cx - b_off, tie_y, cx + b_off, tie_y, pre_cb_active);
        
        /* Center vertical down to CB */
        draw_bay_feeder(cr, cx, tie_y, SLD_CB_Y - cb_s, pre_cb_active);
        
        /* Bottom of CB down to top of Line DS */
        draw_bay_feeder(cr, cx, SLD_CB_Y + cb_s, SLD_LINE_DS_Y - ds_s, post_cb_active);

        /* Bottom of Line DS down to ES branch level */
        draw_bay_feeder(cr, cx, SLD_LINE_DS_Y + ds_s, SLD_EARTH_Y, post_line_active);
        
        /* Continue down to the feeder label */
        draw_bay_feeder(cr, cx, SLD_EARTH_Y, SLD_FEEDER_Y, post_line_active);

        double label_offset = 60.0;
        if (strstr(bay->name, "TRF") || b < 3) {
            draw_ansi_transformer(cr, cx, SLD_FEEDER_Y);
        } else {
            draw_downward_arrow(cr, cx, SLD_FEEDER_Y);
        }

        gboolean is_conn = (bay_connected && bay_connected[b]);
        draw_bay_label(cr, bay, cx, label_offset, is_conn);
    }

    /* Draw all equipment symbols */
    for (int i = 0; i < count; i++) {
        SldEquipment *e = &equip[i];
        switch (e->type) {
        case EQUIP_CB:
            draw_cb(cr, e);
            break;
        case EQUIP_DS_BUS:
        case EQUIP_DS_LINE:
            draw_ds(cr, e);
            break;
        case EQUIP_EARTH:
            draw_earth(cr, e);
            break;
        default:
            break;
        }
        draw_equip_label(cr, e);
    }

    /* Restore coordinate system so HUD/Legend elements anchor to viewport */
    cairo_restore(cr);

    /* Legend - pinned to bottom of visible viewport in all display modes */
    double lx = 20.0;
    double ly = (height > 36.0) ? (height - 34.0) : 10.0;

    /* Styled glassmorphic container badge for the Legend */
    draw_rounded_rect(cr, lx - 8.0, ly - 6.0, 480.0, 24.0, 6.0);
    cairo_set_source_rgba(cr, 0.05, 0.07, 0.10, 0.88);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.25, 0.30, 0.40, 0.45);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    /* OPEN: Green Outline with NO fill */
    cairo_set_source_rgb(cr, 0.15, 0.88, 0.35);
    cairo_set_line_width(cr, 1.8);
    cairo_rectangle(cr, lx, ly, 12, 12);
    cairo_stroke(cr);
    cairo_set_source_rgb(cr, 0.75, 0.75, 0.80);
    draw_text_pango(cr, "OPEN", "Inter Bold 9", lx + 16, ly, FALSE, FALSE);

    /* CLOSED: Electric Cyan Filled */
    cairo_set_source_rgb(cr, 0.0, 0.94, 1.0);
    cairo_rectangle(cr, lx + 65, ly, 12, 12);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.75, 0.75, 0.80);
    draw_text_pango(cr, "CLOSED", "Inter Bold 9", lx + 81, ly, FALSE, FALSE);

    /* INTERMEDIATE: Dark Yellow / Deep Amber Outline */
    cairo_set_source_rgb(cr, 1.0, 0.72, 0.0);
    cairo_set_line_width(cr, 1.8);
    cairo_rectangle(cr, lx + 145, ly, 12, 12);
    cairo_stroke(cr);
    cairo_set_source_rgb(cr, 0.75, 0.75, 0.80);
    draw_text_pango(cr, "INTERMEDIATE", "Inter Bold 9", lx + 161, ly, FALSE, FALSE);

    /* Vertical separator divider */
    cairo_set_source_rgba(cr, 0.35, 0.40, 0.50, 0.60);
    cairo_move_to(cr, lx + 268, ly - 2);
    cairo_line_to(cr, lx + 268, ly + 14);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    /* Server Comms Indicators in Legend */
    cairo_set_source_rgb(cr, 0.1, 0.98, 0.55);
    cairo_arc(cr, lx + 284, ly + 6, 5, 0, 2 * M_PI); cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.35, 0.95, 0.65);
    draw_text_pango(cr, "COMM OK", "Inter Bold 9", lx + 296, ly, FALSE, FALSE);

    cairo_set_source_rgb(cr, 0.95, 0.3, 0.3);
    cairo_arc(cr, lx + 375, ly + 6, 5, 0, 2 * M_PI); cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.85, 0.45, 0.45);
    draw_text_pango(cr, "OFFLINE", "Inter Bold 9", lx + 387, ly, FALSE, FALSE);
}
