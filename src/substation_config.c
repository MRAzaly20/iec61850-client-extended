/* ============================================================
 *  substation_config.c — Substation bay & apparatus configuration
 * ============================================================ */
#include "substation_config.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
bay_add_equip(SldBayConfig *b, const char *key, const char *label,
              const char *ln, int equip_type, double y_slot, double x_off)
{
    if (!b || b->num_equipment >= SLD_MAX_EQUIP_PER_BAY) return;
    if (!key || !ln || !ln[0]) return;
    SldEquipConfig *e = &b->equipment[b->num_equipment++];
    snprintf(e->key, sizeof(e->key), "%s", key);
    snprintf(e->label, sizeof(e->label), "%s", label ? label : key);
    snprintf(e->ln, sizeof(e->ln), "%s", ln);
    snprintf(e->do_name, sizeof(e->do_name), "Pos");
    snprintf(e->da_name, sizeof(e->da_name), "stVal");
    snprintf(e->fc_read, sizeof(e->fc_read), "ST");
    e->equip_type = equip_type;
    e->y_slot = y_slot;
    e->x_off = x_off;
}

static void
bay_fill_legacy_equipment(SldBayConfig *b)
{
    b->num_equipment = 0;
    if (b->ln_ds_a[0])
        bay_add_equip(b, "DS-A", "DS-A", b->ln_ds_a, 1, 0.0, SLD_X_AUTO);
    if (b->ln_ds_b[0])
        bay_add_equip(b, "DS-B", "DS-B", b->ln_ds_b, 1, 0.0, SLD_X_AUTO);
    if (b->ln_cb[0])
        bay_add_equip(b, "CB1", "CB1", b->ln_cb, 0, 0.0, SLD_X_AUTO);
    if (b->has_line_ds && b->ln_line_ds[0])
        bay_add_equip(b, "DSL", "Line DS", b->ln_line_ds, 2, 0.0, SLD_X_AUTO);
    if (b->has_earth && b->ln_earth[0])
        bay_add_equip(b, "ES", "ES", b->ln_earth, 3, 0.0, SLD_X_AUTO);
}

void substation_config_init_default(SubstationConfig *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));

    snprintf(cfg->substation_name, sizeof(cfg->substation_name), "GI 150kV Switchyard");
    cfg->num_bays = 6;

    const char *bay_names[6] = {
        "E01\nTRF-1", "E02\nTRF-2", "E03\nTRF-3",
        "E04\nCOUPLER", "E05\nLINE-1", "E06\nLINE-2"
    };

    for (int i = 0; i < 6; i++) {
        SldBayConfig *b = &cfg->bays[i];
        snprintf(b->name, sizeof(b->name), "%s", bay_names[i]);
        snprintf(b->ied_name, sizeof(b->ied_name), "E%02d_BCU", i + 1);
        snprintf(b->ld_inst, sizeof(b->ld_inst), "E%02dBCU", i + 1);
        snprintf(b->host, sizeof(b->host), "127.0.0.1");
        b->port = 10201 + i;
        snprintf(b->rcb_ref, sizeof(b->rcb_ref), "E%02d_BCUE%02dBCU/LLN0.RP.SwitchRCB01", i + 1, i + 1);

        snprintf(b->ln_cb, sizeof(b->ln_cb), "XCBR1");
        snprintf(b->ln_ds_a, sizeof(b->ln_ds_a), "XSWI1");
        snprintf(b->ln_ds_b, sizeof(b->ln_ds_b), "XSWI2");

        if (i == 3) {
            /* Bay 4 Coupler: no line disconnector or earth switch */
            b->ln_line_ds[0] = '\0';
            b->ln_earth[0] = '\0';
            b->has_line_ds = 0;
            b->has_earth = 0;
        } else {
            snprintf(b->ln_line_ds, sizeof(b->ln_line_ds), "XSWI3");
            snprintf(b->ln_earth, sizeof(b->ln_earth), "XSWI4");
            b->has_line_ds = 1;
            b->has_earth = 1;
        }

        b->cx = 120.0 + i * 200.0;
        bay_fill_legacy_equipment(b);
    }
}

void
bay_sync_equipment_from_legacy(SldBayConfig *b)
{
    if (!b) return;
    if (b->num_equipment == 0) {
        bay_fill_legacy_equipment(b);
        return;
    }
    int ds_count = 0;
    for (int i = 0; i < b->num_equipment; i++) {
        SldEquipConfig *e = &b->equipment[i];
        if (e->equip_type == 0) {
            if (b->ln_cb[0]) snprintf(e->ln, sizeof(e->ln), "%s", b->ln_cb);
        } else if (e->equip_type == 1) {
            if (ds_count == 0 && b->ln_ds_a[0]) {
                snprintf(e->ln, sizeof(e->ln), "%s", b->ln_ds_a);
            } else if (ds_count == 1 && b->ln_ds_b[0]) {
                snprintf(e->ln, sizeof(e->ln), "%s", b->ln_ds_b);
            }
            ds_count++;
        } else if (e->equip_type == 2) {
            if (b->ln_line_ds[0]) snprintf(e->ln, sizeof(e->ln), "%s", b->ln_line_ds);
        } else if (e->equip_type == 3) {
            if (b->ln_earth[0]) snprintf(e->ln, sizeof(e->ln), "%s", b->ln_earth);
        }
    }
}

static void
bay_sync_legacy_from_equipment(SldBayConfig *b)
{
    b->ln_cb[0] = 0; b->ln_ds_a[0] = 0; b->ln_ds_b[0] = 0;
    b->ln_line_ds[0] = 0; b->ln_earth[0] = 0;
    b->has_line_ds = 0; b->has_earth = 0;
    for (int i = 0; i < b->num_equipment; i++) {
        SldEquipConfig *e = &b->equipment[i];
        if (e->equip_type == 0 && b->ln_cb[0] == 0)
            snprintf(b->ln_cb, sizeof(b->ln_cb), "%s", e->ln);
        else if (e->equip_type == 1 && b->ln_ds_a[0] == 0)
            snprintf(b->ln_ds_a, sizeof(b->ln_ds_a), "%s", e->ln);
        else if (e->equip_type == 1)
            snprintf(b->ln_ds_b, sizeof(b->ln_ds_b), "%s", e->ln);
        else if (e->equip_type == 2) {
            snprintf(b->ln_line_ds, sizeof(b->ln_line_ds), "%s", e->ln);
            b->has_line_ds = 1;
        } else if (e->equip_type == 3) {
            snprintf(b->ln_earth, sizeof(b->ln_earth), "%s", e->ln);
            b->has_earth = 1;
        }
    }
}

int substation_config_save(const char *filepath, const SubstationConfig *cfg) {
    if (!filepath || !cfg) return -1;

    cJSON *root = cJSON_CreateObject();
    if (!root) return -1;

    cJSON_AddStringToObject(root, "substation_name", cfg->substation_name);
    cJSON_AddNumberToObject(root, "num_bays", cfg->num_bays);

    cJSON *bays_arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "bays", bays_arr);

    for (int i = 0; i < cfg->num_bays; i++) {
        const SldBayConfig *b = &cfg->bays[i];
        cJSON *bobj = cJSON_CreateObject();

        cJSON_AddStringToObject(bobj, "name", b->name);
        cJSON_AddStringToObject(bobj, "ied_name", b->ied_name);
        cJSON_AddStringToObject(bobj, "ld_inst", b->ld_inst);
        cJSON_AddStringToObject(bobj, "host", b->host);
        cJSON_AddNumberToObject(bobj, "port", b->port);
        cJSON_AddStringToObject(bobj, "rcb_ref", b->rcb_ref);

        cJSON_AddStringToObject(bobj, "ln_cb", b->ln_cb);
        cJSON_AddStringToObject(bobj, "ln_ds_a", b->ln_ds_a);
        cJSON_AddStringToObject(bobj, "ln_ds_b", b->ln_ds_b);
        cJSON_AddStringToObject(bobj, "ln_line_ds", b->ln_line_ds);
        cJSON_AddStringToObject(bobj, "ln_earth", b->ln_earth);
        cJSON *eq_arr = cJSON_CreateArray();
        cJSON_AddItemToObject(bobj, "equipment", eq_arr);
        for (int k = 0; k < b->num_equipment; k++) {
            const SldEquipConfig *e = &b->equipment[k];
            cJSON *eobj = cJSON_CreateObject();
            cJSON_AddStringToObject(eobj, "key", e->key);
            cJSON_AddStringToObject(eobj, "label", e->label);
            cJSON_AddStringToObject(eobj, "ln", e->ln);
            cJSON_AddStringToObject(eobj, "do_name", e->do_name[0] ? e->do_name : "Pos");
            cJSON_AddStringToObject(eobj, "da_name", e->da_name[0] ? e->da_name : "stVal");
            cJSON_AddStringToObject(eobj, "fc_read", e->fc_read[0] ? e->fc_read : "ST");
            cJSON_AddNumberToObject(eobj, "equip_type", e->equip_type);
            cJSON_AddNumberToObject(eobj, "y_slot", e->y_slot);
            cJSON_AddNumberToObject(eobj, "x_off", e->x_off);
            cJSON_AddItemToArray(eq_arr, eobj);
        }

        cJSON_AddBoolToObject(bobj, "has_line_ds", b->has_line_ds);
        cJSON_AddBoolToObject(bobj, "has_earth", b->has_earth);
        cJSON_AddNumberToObject(bobj, "cx", b->cx);

        cJSON_AddItemToArray(bays_arr, bobj);
    }

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_str) return -1;

    FILE *f = fopen(filepath, "w");
    if (!f) {
        free(json_str);
        return -1;
    }

    fputs(json_str, f);
    fclose(f);
    free(json_str);
    return 0;
}

int substation_config_load(const char *filepath, SubstationConfig *cfg) {
    if (!filepath || !cfg) return -1;

    FILE *f = fopen(filepath, "r");
    if (!f) {
        /* Fallback: try parent directory if running from build/ */
        char alt_path[256];
        snprintf(alt_path, sizeof(alt_path), "../%s", filepath);
        f = fopen(alt_path, "r");
    }
    if (!f) {
        /* If file does not exist, initialize defaults and create it */
        substation_config_init_default(cfg);
        substation_config_save(filepath, cfg);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) {
        fclose(f);
        substation_config_init_default(cfg);
        return 0;
    }

    char *buf = (char *)malloc(len + 1);
    if (!buf) {
        fclose(f);
        substation_config_init_default(cfg);
        return 0;
    }

    size_t read_bytes = fread(buf, 1, len, f);
    buf[read_bytes] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);

    if (!root) {
        substation_config_init_default(cfg);
        return 0;
    }

    memset(cfg, 0, sizeof(*cfg));

    cJSON *sub_name = cJSON_GetObjectItem(root, "substation_name");
    if (sub_name && sub_name->valuestring) {
        snprintf(cfg->substation_name, sizeof(cfg->substation_name), "%s", sub_name->valuestring);
    } else {
        snprintf(cfg->substation_name, sizeof(cfg->substation_name), "GI 150kV Switchyard");
    }

    cJSON *bays_arr = cJSON_GetObjectItem(root, "bays");
    if (bays_arr && cJSON_IsArray(bays_arr)) {
        int count = cJSON_GetArraySize(bays_arr);
        if (count > SLD_MAX_BAYS) count = SLD_MAX_BAYS;
        cfg->num_bays = count;

        for (int i = 0; i < count; i++) {
            cJSON *bobj = cJSON_GetArrayItem(bays_arr, i);
            if (!bobj) continue;

            SldBayConfig *b = &cfg->bays[i];

            cJSON *item = cJSON_GetObjectItem(bobj, "name");
            if (item && item->valuestring) snprintf(b->name, sizeof(b->name), "%s", item->valuestring);

            item = cJSON_GetObjectItem(bobj, "ied_name");
            if (item && item->valuestring) snprintf(b->ied_name, sizeof(b->ied_name), "%s", item->valuestring);

            item = cJSON_GetObjectItem(bobj, "ld_inst");
            if (item && item->valuestring) snprintf(b->ld_inst, sizeof(b->ld_inst), "%s", item->valuestring);

            item = cJSON_GetObjectItem(bobj, "host");
            if (item && item->valuestring) snprintf(b->host, sizeof(b->host), "%s", item->valuestring);
            else snprintf(b->host, sizeof(b->host), "127.0.0.1");

            item = cJSON_GetObjectItem(bobj, "port");
            if (item && cJSON_IsNumber(item)) b->port = item->valueint;
            else b->port = 10201 + i;

            item = cJSON_GetObjectItem(bobj, "rcb_ref");
            if (item && item->valuestring) snprintf(b->rcb_ref, sizeof(b->rcb_ref), "%s", item->valuestring);

            item = cJSON_GetObjectItem(bobj, "ln_cb");
            if (item && item->valuestring) snprintf(b->ln_cb, sizeof(b->ln_cb), "%s", item->valuestring);
            else snprintf(b->ln_cb, sizeof(b->ln_cb), "XCBR1");

            item = cJSON_GetObjectItem(bobj, "ln_ds_a");
            if (item && item->valuestring) snprintf(b->ln_ds_a, sizeof(b->ln_ds_a), "%s", item->valuestring);
            else snprintf(b->ln_ds_a, sizeof(b->ln_ds_a), "XSWI1");

            item = cJSON_GetObjectItem(bobj, "ln_ds_b");
            if (item && item->valuestring) snprintf(b->ln_ds_b, sizeof(b->ln_ds_b), "%s", item->valuestring);
            else snprintf(b->ln_ds_b, sizeof(b->ln_ds_b), "XSWI2");

            item = cJSON_GetObjectItem(bobj, "ln_line_ds");
            if (item && item->valuestring) snprintf(b->ln_line_ds, sizeof(b->ln_line_ds), "%s", item->valuestring);

            item = cJSON_GetObjectItem(bobj, "ln_earth");
            if (item && item->valuestring) snprintf(b->ln_earth, sizeof(b->ln_earth), "%s", item->valuestring);

            item = cJSON_GetObjectItem(bobj, "has_line_ds");
            if (item && cJSON_IsBool(item)) b->has_line_ds = cJSON_IsTrue(item) ? 1 : 0;
            else b->has_line_ds = (i != 3) ? 1 : 0;

            item = cJSON_GetObjectItem(bobj, "has_earth");
            if (item && cJSON_IsBool(item)) b->has_earth = cJSON_IsTrue(item) ? 1 : 0;
            else b->has_earth = (i != 3) ? 1 : 0;

            item = cJSON_GetObjectItem(bobj, "cx");
            if (item && cJSON_IsNumber(item)) b->cx = item->valuedouble;
            else b->cx = 120.0 + i * 200.0;
            b->num_equipment = 0;
            cJSON *eq_arr = cJSON_GetObjectItem(bobj, "equipment");
            if (eq_arr && cJSON_IsArray(eq_arr)) {
                int n = cJSON_GetArraySize(eq_arr);
                if (n > SLD_MAX_EQUIP_PER_BAY) n = SLD_MAX_EQUIP_PER_BAY;
                for (int k = 0; k < n; k++) {
                    cJSON *eobj = cJSON_GetArrayItem(eq_arr, k);
                    if (!eobj) continue;
                    SldEquipConfig *e = &b->equipment[b->num_equipment++];
                    cJSON *f;
                    f = cJSON_GetObjectItem(eobj, "key");
                    snprintf(e->key, sizeof(e->key), "%s",
                        (f && f->valuestring) ? f->valuestring : "EQ");
                    f = cJSON_GetObjectItem(eobj, "label");
                    snprintf(e->label, sizeof(e->label), "%s",
                        (f && f->valuestring) ? f->valuestring : e->key);
                    f = cJSON_GetObjectItem(eobj, "ln");
                    snprintf(e->ln, sizeof(e->ln), "%s",
                        (f && f->valuestring) ? f->valuestring : "");
                    f = cJSON_GetObjectItem(eobj, "do_name");
                    snprintf(e->do_name, sizeof(e->do_name), "%s",
                        (f && f->valuestring) ? f->valuestring : "Pos");
                    f = cJSON_GetObjectItem(eobj, "da_name");
                    snprintf(e->da_name, sizeof(e->da_name), "%s",
                        (f && f->valuestring) ? f->valuestring : "stVal");
                    f = cJSON_GetObjectItem(eobj, "fc_read");
                    snprintf(e->fc_read, sizeof(e->fc_read), "%s",
                        (f && f->valuestring) ? f->valuestring : "ST");
                    f = cJSON_GetObjectItem(eobj, "equip_type");
                    e->equip_type = (f && cJSON_IsNumber(f)) ? f->valueint : 1;
                    f = cJSON_GetObjectItem(eobj, "y_slot");
                    e->y_slot = (f && cJSON_IsNumber(f)) ? f->valuedouble : 0.0;
                    f = cJSON_GetObjectItem(eobj, "x_off");
                    e->x_off = (f && cJSON_IsNumber(f)) ? f->valuedouble : SLD_X_AUTO;
                }
                if (b->num_equipment > 0) {
                    if (b->ln_cb[0] || b->ln_ds_a[0]) {
                        bay_sync_equipment_from_legacy(b);
                    } else {
                        bay_sync_legacy_from_equipment(b);
                    }
                } else {
                    bay_fill_legacy_equipment(b);
                }
            } else {
                bay_fill_legacy_equipment(b);
            }
        }
    } else {
        substation_config_init_default(cfg);
    }

    cJSON_Delete(root);
    return 0;
}
