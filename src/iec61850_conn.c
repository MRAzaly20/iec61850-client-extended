/* ============================================================
 *  iec61850_conn.c — Connection, discovery, and reporting logic
 * ============================================================ */
#include "iec61850_app.h"

typedef struct {
    char doRef[512];
    IedConnection con;
    ControlObjectClient ctlObj;
    gint64 select_time_ms;
} ActiveControlSession;

static ActiveControlSession g_active_session = {0};
static GMutex g_active_session_mutex;

#include "iec61850_client.h"
#include "iec61850_common.h"
#include "hal_thread.h"
#include "linked_list.h"
#include "sld_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/*  Data-model tree helpers                                           */
/* ------------------------------------------------------------------ */

static IedModelNode *
ied_model_node_new(const char *name, const char *ref, NodeType type)
{
    IedModelNode *n = g_new0(IedModelNode, 1);
    n->name     = g_strdup(name);
    n->ref      = g_strdup(ref);
    n->type     = type;
    n->children = g_ptr_array_new_with_free_func((GDestroyNotify)ied_model_node_free);
    return n;
}

void
ied_model_node_free(IedModelNode *node)
{
    if (!node) return;
    g_free(node->name);
    g_free(node->ref);
    g_free(node->fc);
    g_free(node->value);
    g_free(node->mms_type);
    g_free(node->basic_type);
    g_free(node->description);
    g_free(node->timestamp);
    if (node->children)
        g_ptr_array_unref(node->children);
    g_free(node);
}

/* ------------------------------------------------------------------ */
/*  Recursive Data-Attribute discovery                                */
/* ------------------------------------------------------------------ */

static void
discover_data_attributes(IedConnection con, IedModelNode *parent, const char *doRef)
{
    IedClientError err;
    LinkedList daList = IedConnection_getDataDirectoryFC(con, &err, doRef);
    if (!daList) return;

    LinkedList entry = LinkedList_getNext(daList);
    while (entry) {
        const char *daName = (const char *)entry->data;

        /* Extract FC if present (libiec61850 appends [FC] to the name) */
        char cleanName[128];
        char fcStr[8] = "";
        const char *bracket = strchr(daName, '[');
        if (bracket) {
            size_t nameLen = (size_t)(bracket - daName);
            if (nameLen >= sizeof(cleanName)) nameLen = sizeof(cleanName) - 1;
            memcpy(cleanName, daName, nameLen);
            cleanName[nameLen] = '\0';
            /* Extract FC between [ and ] */
            const char *end = strchr(bracket, ']');
            if (end && (end - bracket - 1) < (int)sizeof(fcStr)) {
                memcpy(fcStr, bracket + 1, (size_t)(end - bracket - 1));
                fcStr[end - bracket - 1] = '\0';
            }
        } else {
            snprintf(cleanName, sizeof(cleanName), "%s", daName);
        }

        char daRef[256];
        snprintf(daRef, sizeof(daRef), "%s.%s", doRef, cleanName);

        IedModelNode *da = ied_model_node_new(cleanName, daRef, NODE_DA);
        if (fcStr[0])
            da->fc = g_strdup(fcStr);

        g_ptr_array_add(parent->children, da);

        /* recurse — DAs can have sub-DAs (structured types) */
        discover_data_attributes(con, da, daRef);

        entry = LinkedList_getNext(entry);
    }
    LinkedList_destroy(daList);
}

/* ------------------------------------------------------------------ */
/*  Full model discovery  (LD → LN → DO → DA)                        */
/* ------------------------------------------------------------------ */

IedModelNode *
discover_model_con(IedConnection con)
{
    IedClientError err;

    IedModelNode *root = ied_model_node_new("Server", "", NODE_ROOT);

    /* --- Logical Devices --- */
    LinkedList deviceList = IedConnection_getLogicalDeviceList(con, &err);
    if (!deviceList) {
        g_printerr("Failed to get logical device list (err %d)\n", err);
        return root;
    }

    LinkedList dev = LinkedList_getNext(deviceList);
    while (dev) {
        const char *ldName = (const char *)dev->data;
        IedModelNode *ldNode = ied_model_node_new(ldName, ldName, NODE_LD);
        g_ptr_array_add(root->children, ldNode);

        /* --- Logical Nodes --- */
        LinkedList lnList = IedConnection_getLogicalDeviceDirectory(con, &err, ldName);
        if (lnList) {
            LinkedList ln = LinkedList_getNext(lnList);
            while (ln) {
                const char *lnName = (const char *)ln->data;
                char lnRef[256];
                snprintf(lnRef, sizeof(lnRef), "%s/%s", ldName, lnName);

                IedModelNode *lnNode = ied_model_node_new(lnName, lnRef, NODE_LN);
                g_ptr_array_add(ldNode->children, lnNode);

                /* --- Data Objects --- */
                LinkedList doList = IedConnection_getLogicalNodeDirectory(
                    con, &err, lnRef, ACSI_CLASS_DATA_OBJECT);
                if (doList) {
                    LinkedList dobj = LinkedList_getNext(doList);
                    while (dobj) {
                        const char *doName = (const char *)dobj->data;
                        char doRef[256];
                        snprintf(doRef, sizeof(doRef), "%s/%s.%s", ldName, lnName, doName);

                        IedModelNode *doNode = ied_model_node_new(doName, doRef, NODE_DO);
                        g_ptr_array_add(lnNode->children, doNode);

                        /* --- Data Attributes (recursive) --- */
                        discover_data_attributes(con, doNode, doRef);

                        dobj = LinkedList_getNext(dobj);
                    }
                    LinkedList_destroy(doList);
                }

                ln = LinkedList_getNext(ln);
            }
            LinkedList_destroy(lnList);
        }

        dev = LinkedList_getNext(dev);
    }
    LinkedList_destroy(deviceList);

    return root;
}

/* ------------------------------------------------------------------ */
/*  Report callback  (runs on libiec61850 protocol thread!)           */
/* ------------------------------------------------------------------ */

static gboolean
update_ui_from_report(gpointer user_data)
{
    ReportCallbackData *rd = (ReportCallbackData *)user_data;
    AppState *app = *((AppState **)(rd + 1));

    char msg[512];
    snprintf(msg, sizeof(msg), "  Report: %s  (%d entries)", rd->rcb_ref, rd->count);
    set_status(app, msg, "status-info");

    g_print("[REPORT PUSH] Received from %s with %d entries\n", rd->rcb_ref, rd->count);

    /* Trigger background refresh of the SLD instantly */
    app_read_all_values(app);

    g_free(rd);
    return G_SOURCE_REMOVE;
}

static void
report_callback(void *parameter, ClientReport report)
{
    AppState *app = (AppState *)parameter;
    MmsValue *dsValues = ClientReport_getDataSetValues(report);

    size_t alloc = sizeof(ReportCallbackData) + sizeof(AppState *);
    ReportCallbackData *rd = (ReportCallbackData *)g_malloc0(alloc);

    const char *rcbRef = ClientReport_getRcbReference(report);
    if (rcbRef)
        snprintf(rd->rcb_ref, sizeof(rd->rcb_ref), "%s", rcbRef);

    rd->count = 0;
    if (dsValues) {
        int n = MmsValue_getArraySize(dsValues);
        if (n > REPORT_MAX_ENTRIES) n = REPORT_MAX_ENTRIES;
        for (int i = 0; i < n; i++) {
            ReasonForInclusion reason = ClientReport_getReasonForInclusion(report, i);
            if (reason != IEC61850_REASON_NOT_INCLUDED) {
                MmsValue *val = MmsValue_getElement(dsValues, i);
                if (val) {
                    MmsValue_printToBuffer(val, rd->entries[rd->count], REPORT_ENTRY_LEN);
                    rd->count++;
                }
            }
        }
    }

    *((AppState **)(rd + 1)) = app;
    g_idle_add(update_ui_from_report, rd);
}

/* ------------------------------------------------------------------ */
/*  Subscribe to Unbuffered Report Control Blocks                     */
/* ------------------------------------------------------------------ */

static void
subscribe_reports_con(IedConnection con, IedModelNode *model_root,
                      GPtrArray *active_rcbs, AppState *app)
{
    IedClientError err;
    if (!model_root) return;

    for (guint i = 0; i < model_root->children->len; i++) {
        IedModelNode *ld = g_ptr_array_index(model_root->children, i);
        for (guint j = 0; j < ld->children->len; j++) {
            IedModelNode *ln = g_ptr_array_index(ld->children, j);

            LinkedList rcbList = IedConnection_getLogicalNodeDirectory(
                con, &err, ln->ref, ACSI_CLASS_URCB);
            if (!rcbList) continue;

            LinkedList entry = LinkedList_getNext(rcbList);
            while (entry) {
                const char *rcbName = (const char *)entry->data;
                char rcbRef[256];
                snprintf(rcbRef, sizeof(rcbRef), "%s.RP.%s", ln->ref, rcbName);

                g_print("  Subscribing to URCB: %s\n", rcbRef);

                ClientReportControlBlock rcb
                    = IedConnection_getRCBValues(con, &err, rcbRef, NULL);

                if (rcb && err == IED_ERROR_OK) {
                    IedConnection_installReportHandler(
                        con, rcbRef,
                        ClientReportControlBlock_getRptId(rcb),
                        report_callback, app);

                    ClientReportControlBlock_setTrgOps(rcb,
                        TRG_OPT_DATA_CHANGED | TRG_OPT_QUALITY_CHANGED
                        | TRG_OPT_INTEGRITY | TRG_OPT_GI);
                    ClientReportControlBlock_setRptEna(rcb, true);
                    ClientReportControlBlock_setIntgPd(rcb, 5000);

                    IedConnection_setRCBValues(con, &err, rcb,
                        RCB_ELEMENT_RPT_ENA | RCB_ELEMENT_TRG_OPS | RCB_ELEMENT_INTG_PD,
                        true);

                    if (err == IED_ERROR_OK) {
                        g_ptr_array_add(active_rcbs, rcb);
                    } else {
                        g_printerr("  Failed to enable RCB %s (err %d)\n", rcbRef, err);
                        ClientReportControlBlock_destroy(rcb);
                    }
                } else {
                    if (rcb) ClientReportControlBlock_destroy(rcb);
                    g_printerr("  Failed to read RCB %s (err %d)\n", rcbRef, err);
                }

                entry = LinkedList_getNext(entry);
            }
            LinkedList_destroy(rcbList);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Connect / Disconnect                                              */
/* ------------------------------------------------------------------ */

void
app_connect(AppState *app)
{
    IedClientError err;

    /* Read host/port from the GUI entry fields */
    const char *host = gtk_editable_get_text(GTK_EDITABLE(app->host_entry));
    int port = atoi(gtk_editable_get_text(GTK_EDITABLE(app->port_entry)));

    if (app->explorer_connected) return;

    app->explorer_con = IedConnection_create();
    IedConnection_setConnectTimeout(app->explorer_con, 2000);
    IedConnection_setRequestTimeout(app->explorer_con, 2000);

    g_print("Connecting to %s:%d ...\n", host, port);
    IedConnection_connect(app->explorer_con, &err, host, port);

    if (err != IED_ERROR_OK) {
        g_printerr("Connection failed (err %d)\n", err);
        IedConnection_destroy(app->explorer_con);
        app->explorer_con = NULL;
        set_status(app, "  Connection failed", "status-disconnected");
        return;
    }

    app->explorer_connected = TRUE;

    /* Discover model */
    IedModelNode *model = discover_model_con(app->explorer_con);

    if (!app->ui_model_root) {
        app->ui_model_root = ied_model_node_new("IED", "", NODE_ROOT);
    } else {
        /* Clear old children if reconnecting */
        g_ptr_array_set_size(app->ui_model_root->children, 0);
    }

    if (model && model->children->len > 0) {
        for (guint c = 0; c < model->children->len; c++) {
            IedModelNode *ld = g_ptr_array_index(model->children, c);
            g_ptr_array_add(app->ui_model_root->children, ld);
        }
        /* Free the shell of the temp model (children are now owned by ui_model_root) */
        g_free(model->name);
        g_free(model->ref);
        g_ptr_array_free(model->children, FALSE);  /* FALSE = don't free elements */
        g_free(model);
    } else {
        if (model) ied_model_node_free(model);
    }

    populate_tree_view(app);

    char msg[256];
    snprintf(msg, sizeof(msg), "  Connected to %s:%d", host, port);
    set_status(app, msg, "status-connected");
}

void
app_disconnect(AppState *app)
{
    g_mutex_lock(&g_active_session_mutex);
    if (g_active_session.ctlObj) {
        ControlObjectClient_destroy(g_active_session.ctlObj);
        g_active_session.ctlObj = NULL;
    }
    g_mutex_unlock(&g_active_session_mutex);
    if (app->explorer_connected && app->explorer_con) {
        IedConnection_close(app->explorer_con);
        IedConnection_destroy(app->explorer_con);
        app->explorer_con = NULL;
        app->explorer_connected = FALSE;
    }

    if (app->ui_model_root) {
        ied_model_node_free(app->ui_model_root);
        app->ui_model_root = NULL;
    }

    g_list_store_remove_all(app->tree_store);
    g_list_store_remove_all(app->data_store);
    app->selected_ln = NULL;

    set_status(app, "  Disconnected", "status-disconnected");
}

/* ------------------------------------------------------------------ */
/*  SLD Bay Connections (separate from explorer)                       */
/* ------------------------------------------------------------------ */

void
sld_connect_bays(AppState *app)
{
    IedClientError err;
    int num = app->sub_config.num_bays;
    for (int i = 0; i < num && i < SLD_MAX_BAYS; i++) {
        BayConnection *bay = &app->bays_con[i];
        if (bay->connected) continue;

        bay->con = IedConnection_create();
        IedConnection_setConnectTimeout(bay->con, 500);
        IedConnection_setRequestTimeout(bay->con, 500);

        g_print("[SLD] Connecting bay %d: %s:%d ...\n", i, bay->host, bay->port);
        IedConnection_connect(bay->con, &err, bay->host, bay->port);

        if (err != IED_ERROR_OK) {
            IedConnection_destroy(bay->con);
            bay->con = NULL;
            bay->connected = FALSE;
            continue;
        }

        bay->connected = TRUE;
        g_print("[SLD] Bay %d connected\n", i);
        
        if (!bay->active_rcbs) {
            bay->active_rcbs = g_ptr_array_new_with_free_func((GDestroyNotify)ClientReportControlBlock_destroy);
        }

        const char *rcbRef = app->sub_config.bays[i].rcb_ref;
        if (rcbRef && rcbRef[0] != '\0') {
            g_print("  Attempting to subscribe to URCB: %s\n", rcbRef);
            ClientReportControlBlock rcb = IedConnection_getRCBValues(bay->con, &err, rcbRef, NULL);
            
            if (rcb && err == IED_ERROR_OK) {
                IedConnection_installReportHandler(bay->con, rcbRef, 
                    ClientReportControlBlock_getRptId(rcb), report_callback, app);

                ClientReportControlBlock_setTrgOps(rcb, TRG_OPT_DATA_CHANGED | TRG_OPT_QUALITY_CHANGED);
                ClientReportControlBlock_setRptEna(rcb, true);
                ClientReportControlBlock_setIntgPd(rcb, 5000);

                IedConnection_setRCBValues(bay->con, &err, rcb,
                    RCB_ELEMENT_RPT_ENA | RCB_ELEMENT_TRG_OPS | RCB_ELEMENT_INTG_PD, true);

                if (err == IED_ERROR_OK) {
                    g_ptr_array_add(bay->active_rcbs, rcb);
                    g_print("  Successfully subscribed to %s\n", rcbRef);
                } else {
                    g_printerr("  Failed to enable RCB %s (err %d)\n", rcbRef, err);
                    ClientReportControlBlock_destroy(rcb);
                }
            } else {
                if (rcb) ClientReportControlBlock_destroy(rcb);
                g_printerr("  Failed to read RCB %s (err %d)\n", rcbRef, err);
            }
        }
    }
}

void
sld_disconnect_bays(AppState *app)
{
    int num = app->sub_config.num_bays;
    for (int i = 0; i < num && i < SLD_MAX_BAYS; i++) {
        BayConnection *bay = &app->bays_con[i];
        if (!bay->connected) continue;

        if (bay->active_rcbs) {
            g_ptr_array_unref(bay->active_rcbs);
            bay->active_rcbs = NULL;
        }

        IedConnection_close(bay->con);
        IedConnection_destroy(bay->con);
        bay->con = NULL;
        bay->connected = FALSE;
    }
}

void
app_reconnect_bays(AppState *app)
{
    g_print("[CONFIG] Reconnecting bays according to updated configuration...\n");
    sld_disconnect_bays(app);

    for (int i = 0; i < app->sub_config.num_bays && i < SLD_MAX_BAYS; i++) {
        g_free(app->bays_con[i].host);
        app->bays_con[i].host = g_strdup(app->sub_config.bays[i].host);
        app->bays_con[i].port = app->sub_config.bays[i].port;
        if (!app->bays_con[i].active_rcbs)
            app->bays_con[i].active_rcbs = g_ptr_array_new();
    }

    sld_init_equipment(app->sld_equipment, &app->sld_equipment_count, &app->sub_config);
    sld_connect_bays(app);
    app_read_all_values(app);

    if (app->sld_area)
        gtk_widget_queue_draw(app->sld_area);
}

/* ------------------------------------------------------------------ */
/*  Read all DA values from server                                    */
/* ------------------------------------------------------------------ */

static const char *
mms_type_name(MmsType t)
{
    switch (t) {
        case MMS_BOOLEAN:            return "MmsBoolean";
        case MMS_FLOAT:              return "MmsFloat";
        case MMS_INTEGER:            return "MmsInteger";
        case MMS_UNSIGNED:           return "MmsUnsigned";
        case MMS_VISIBLE_STRING:     return "MmsVisibleString";
        case MMS_OCTET_STRING:       return "MmsOctetString";
        case MMS_UTC_TIME:           return "MmsUtcTime";
        case MMS_BIT_STRING:         return "MmsBitString";
        case MMS_STRING:             return "MmsString";
        case MMS_BINARY_TIME:        return "MmsBinaryTime";
        case MMS_STRUCTURE:          return "MmsStructure";
        case MMS_ARRAY:              return "MmsArray";
        case MMS_DATA_ACCESS_ERROR:  return "AccessError";
        default:                     return "Unknown";
    }
}

/* ------------------------------------------------------------------ */
/*  IEC 61850 BasicType resolver  (§02/§03 of the spec)                */
/* ------------------------------------------------------------------ */

/* Resolve IEC 61850 BasicType from DA name + MMS type.
 * Uses well-known DA names per CDC tables in §03, with MMS-type fallback. */
static const char *
resolve_basic_type(const char *da_name, MmsType mms_t, int bit_count)
{
    if (!da_name) goto fallback;

    /* --- Well-known DA names (§03 CDC tables) --- */
    if (g_strcmp0(da_name, "q") == 0 || g_strcmp0(da_name, "subQ") == 0)
        return "Quality";
    if (g_strcmp0(da_name, "t") == 0 || g_strcmp0(da_name, "T") == 0 ||
        g_strcmp0(da_name, "frTm") == 0 || g_strcmp0(da_name, "EntryTm") == 0)
        return "Timestamp";
    if (g_strcmp0(da_name, "Check") == 0)
        return "Check";
    if (g_strcmp0(da_name, "ctlModel") == 0)
        return "CtlModels";
    if (g_strcmp0(da_name, "ctlNum") == 0)
        return "INT8U";
    if (g_strcmp0(da_name, "orCat") == 0)
        return "INT8";
    if (g_strcmp0(da_name, "orIdent") == 0)
        return "OCTET_STRING_64";
    if (g_strcmp0(da_name, "origin") == 0)
        return "Originator";
    if (g_strcmp0(da_name, "sboClass") == 0)
        return "Enumerated";
    if (g_strcmp0(da_name, "sboTimeout") == 0)
        return "INT32U";
    if (g_strcmp0(da_name, "d") == 0 || g_strcmp0(da_name, "dU") == 0 ||
        g_strcmp0(da_name, "Descrip") == 0)
        return "VisibleString_255";
    if (g_strcmp0(da_name, "vendor") == 0 || g_strcmp0(da_name, "swRev") == 0 ||
        g_strcmp0(da_name, "hwRev") == 0 || g_strcmp0(da_name, "serNum") == 0 ||
        g_strcmp0(da_name, "model") == 0 || g_strcmp0(da_name, "location") == 0 ||
        g_strcmp0(da_name, "configRev") == 0 || g_strcmp0(da_name, "ldNs") == 0)
        return "VisibleString_255";
    if (g_strcmp0(da_name, "TrgOps") == 0)
        return "TriggerConditions";
    if (g_strcmp0(da_name, "OptFlds") == 0)
        return "OptFlds";
    if (g_strcmp0(da_name, "RptID") == 0 || g_strcmp0(da_name, "DatSet") == 0)
        return "VisibleString_129";
    if (g_strcmp0(da_name, "range") == 0)
        return "Enumerated";
    if (g_strcmp0(da_name, "mag") == 0 || g_strcmp0(da_name, "instMag") == 0 ||
        g_strcmp0(da_name, "setMag") == 0 || g_strcmp0(da_name, "subMag") == 0)
        return "AnalogueValue";
    if (g_strcmp0(da_name, "cVal") == 0 || g_strcmp0(da_name, "instCVal") == 0)
        return "Vector";
    if (g_strcmp0(da_name, "units") == 0)
        return "Unit";
    if (g_strcmp0(da_name, "sVC") == 0)
        return "ScaledValueConfig";
    if (g_strcmp0(da_name, "rangeC") == 0)
        return "RangeConfig";
    if (g_strcmp0(da_name, "valWTr") == 0)
        return "BSCValW";
    if (g_strcmp0(da_name, "Oper") == 0 || g_strcmp0(da_name, "Cancel") == 0 ||
        g_strcmp0(da_name, "SBO") == 0)
        return NULL; /* structured — no single BasicType */

    /* Sub-DA names used in AnalogueValue and elsewhere */
    if (g_strcmp0(da_name, "f") == 0)   return "FLOAT32";
    if (g_strcmp0(da_name, "i") == 0 && mms_t == MMS_INTEGER) return "INT32";
    if (g_strcmp0(da_name, "ang") == 0) return "AnalogueValue";

    /*
     * CDC-dependent DAs: stVal, ctlVal, subVal
     * Per stVal_rule.md and §03:
     *   MmsBoolean  → BOOLEAN   (SPS, SPC, ACT)
     *   MmsInteger  → Enumerated (DPS, ENS, ENC, DPC)  — if values 0–3
     *                 INT32      (INS, INC)             — wider range
     *   MmsFloat    → FLOAT32   (APC setpoint)
     */
    if (g_strcmp0(da_name, "stVal") == 0 ||
        g_strcmp0(da_name, "ctlVal") == 0 ||
        g_strcmp0(da_name, "subVal") == 0) {
        switch (mms_t) {
            case MMS_BOOLEAN:    return "BOOLEAN";
            case MMS_INTEGER:    return "Enumerated";
            case MMS_FLOAT:      return "FLOAT32";
            case MMS_BIT_STRING: return "Dbpos";
            default:             goto fallback;
        }
    }

    /* Always-boolean DAs (per §03, these are BOOLEAN regardless of CDC) */
    if (g_strcmp0(da_name, "Test") == 0 || g_strcmp0(da_name, "subEna") == 0 ||
        g_strcmp0(da_name, "frEna") == 0 || g_strcmp0(da_name, "general") == 0 ||
        g_strcmp0(da_name, "transInd") == 0)
        return "BOOLEAN";

    /* Phase/neutral alarm flags in ACT CDC — always BOOLEAN */
    if ((g_strcmp0(da_name, "phsA") == 0 || g_strcmp0(da_name, "phsB") == 0 ||
         g_strcmp0(da_name, "phsC") == 0 || g_strcmp0(da_name, "neut") == 0) &&
        mms_t == MMS_BOOLEAN)
        return "BOOLEAN";

fallback:
    /* MMS-type based fallback (§02 mapping) */
    switch (mms_t) {
        case MMS_BOOLEAN:        return "BOOLEAN";
        case MMS_FLOAT:          return "FLOAT32";
        case MMS_INTEGER:        return "INT32";
        case MMS_UNSIGNED:       return "INT32U";
        case MMS_VISIBLE_STRING: return "VisibleString";
        case MMS_OCTET_STRING:   return "OCTET_STRING";
        case MMS_UTC_TIME:       return "Timestamp";
        case MMS_BIT_STRING:
            if (bit_count == 13 || bit_count == 16) return "Quality";
            if (bit_count == 2)  return "Check";
            if (bit_count == 5)  return "TriggerConditions";
            if (bit_count == 10) return "OptFlds";
            return "BIT_STRING";
        case MMS_STRUCTURE:      return NULL; /* handled at DO level */
        case MMS_ARRAY:          return "ARRAY";
        default:                 return NULL;
    }
}

/* ------------------------------------------------------------------ */
/*  Dbpos decoding helper                                             */
/* ------------------------------------------------------------------ */

static int decodeDbpos(const MmsValue* dbposVal)
{
    if (dbposVal == NULL) return -1;
    if (MmsValue_getType(dbposVal) == MMS_INTEGER ||
        MmsValue_getType(dbposVal) == MMS_UNSIGNED)
        return MmsValue_toInt32(dbposVal);
        
    if (MmsValue_getType(dbposVal) == MMS_BIT_STRING) {
        int val = 0;
        if (MmsValue_getBitStringBit(dbposVal, 0)) val |= 2; /* MSB */
        if (MmsValue_getBitStringBit(dbposVal, 1)) val |= 1; /* LSB */
        return val;
    }
    
    return -1; /* MMS_DATA_ACCESS_ERROR or unexpected type */
}

/* ------------------------------------------------------------------ */
/*  Enumeration label resolver  (§15 of the spec)                     */
/* ------------------------------------------------------------------ */

static const char *
resolve_enum_label(const char *da_name, int value)
{
    if (g_strcmp0(da_name, "ctlModel") == 0) {
        switch (value) {
            case 0: return "status-only";
            case 1: return "direct-normal";
            case 2: return "SBO-normal";
            case 3: return "direct-enhanced";
            case 4: return "SBO-enhanced";
        }
    }
    if (g_strcmp0(da_name, "stVal") == 0 || g_strcmp0(da_name, "ctlVal") == 0 ||
        g_strcmp0(da_name, "subVal") == 0) {
        /* DPStatusKind (for DPS/DPC — only when MMS type is integer) */
        switch (value) {
            case 0: return "intermediate-state";
            case 1: return "off";
            case 2: return "on";
            case 3: return "bad-state";
        }
    }
    if (g_strcmp0(da_name, "orCat") == 0) {
        switch (value) {
            case 0: return "not-supported";
            case 1: return "bay-control";
            case 2: return "station-control";
            case 3: return "remote-control";
            case 4: return "automatic-bay";
            case 5: return "automatic-station";
            case 6: return "automatic-remote";
            case 7: return "maintenance";
            case 8: return "process";
        }
    }
    if (g_strcmp0(da_name, "range") == 0) {
        switch (value) {
            case 0: return "normal";
            case 1: return "high";
            case 2: return "low";
            case 3: return "high-high";
            case 4: return "low-low";
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Quality bitstring decode  (§06 of the spec)                       */
/* ------------------------------------------------------------------ */

static void
format_quality(MmsValue *val, char *buf, size_t buflen)
{
    uint32_t bits = MmsValue_getBitStringAsInteger(val);
    int validity = (bits >> 11) & 0x3; /* bits 0-1 (MSB first) */
    const char *vstr;
    switch (validity) {
        case 0:  vstr = "good";         break;
        case 1:  vstr = "invalid";      break;
        case 2:  vstr = "reserved";     break;
        case 3:  vstr = "questionable"; break;
        default: vstr = "unknown";      break;
    }

    /* Build detail string for non-zero detail bits */
    char detail[256] = {0};
    int dlen = 0;
    if (bits & (1 << 10)) dlen += snprintf(detail+dlen, sizeof(detail)-dlen, ",overflow");
    if (bits & (1 <<  9)) dlen += snprintf(detail+dlen, sizeof(detail)-dlen, ",outOfRange");
    if (bits & (1 <<  8)) dlen += snprintf(detail+dlen, sizeof(detail)-dlen, ",badReference");
    if (bits & (1 <<  7)) dlen += snprintf(detail+dlen, sizeof(detail)-dlen, ",oscillatory");
    if (bits & (1 <<  6)) dlen += snprintf(detail+dlen, sizeof(detail)-dlen, ",failure");
    if (bits & (1 <<  5)) dlen += snprintf(detail+dlen, sizeof(detail)-dlen, ",oldData");
    if (bits & (1 <<  4)) dlen += snprintf(detail+dlen, sizeof(detail)-dlen, ",inconsistent");
    if (bits & (1 <<  3)) dlen += snprintf(detail+dlen, sizeof(detail)-dlen, ",inaccurate");
    if (bits & (1 <<  2)) dlen += snprintf(detail+dlen, sizeof(detail)-dlen, ",substituted");
    if (bits & (1 <<  1)) dlen += snprintf(detail+dlen, sizeof(detail)-dlen, ",test");
    if (bits & (1 <<  0)) dlen += snprintf(detail+dlen, sizeof(detail)-dlen, ",operatorBlocked");

    if (dlen > 0)
        snprintf(buf, buflen, "%s [%s]", vstr, detail + 1); /* skip leading comma */
    else
        snprintf(buf, buflen, "%s", vstr);
}

/* All FCs we try to read for each DO */
static const FunctionalConstraint READ_FCS[] = {
    IEC61850_FC_ST, IEC61850_FC_MX, IEC61850_FC_CF,
    IEC61850_FC_DC, IEC61850_FC_SP, IEC61850_FC_SV,
    IEC61850_FC_EX, IEC61850_FC_CO
};
static const char *READ_FC_NAMES[] = {
    "ST", "MX", "CF", "DC", "SP", "SV", "EX", "CO"
};
#define NUM_READ_FCS 8

/* ------------------------------------------------------------------ */
/*  Store MmsValue into a model node  (spec-compliant formatting)     */
/* ------------------------------------------------------------------ */

static void
store_mms_value(IedModelNode *node, MmsValue *val)
{
    if (!val) return;
    g_free(node->mms_type);
    node->mms_type = g_strdup(mms_type_name(MmsValue_getType(val)));

    char buf[512] = {0};
    MmsType t = MmsValue_getType(val);

    if (t == MMS_BOOLEAN) {
        snprintf(buf, sizeof(buf), "%s", MmsValue_getBoolean(val) ? "True" : "False");

    } else if (t == MMS_FLOAT) {
        snprintf(buf, sizeof(buf), "%g", MmsValue_toFloat(val));

    } else if (t == MMS_INTEGER) {
        int32_t iv = MmsValue_toInt32(val);
        /* Try enum label if DA name is known */
        const char *label = resolve_enum_label(node->name, iv);
        if (label)
            snprintf(buf, sizeof(buf), "%d (%s)", iv, label);
        else
            snprintf(buf, sizeof(buf), "%d", iv);

    } else if (t == MMS_UNSIGNED) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)MmsValue_toUint32(val));

    } else if (t == MMS_VISIBLE_STRING) {
        const char *s = MmsValue_toString(val);
        snprintf(buf, sizeof(buf), "%s", s ? s : "");

    } else if (t == MMS_UTC_TIME) {
        /* §07: YYYY-MM-DD HH:MM:SS.mmm +00:00 */
        uint64_t ms = MmsValue_getUtcTimeInMs(val);
        time_t sec = (time_t)(ms / 1000);
        unsigned millis = (unsigned)(ms % 1000);
        struct tm *tm = gmtime(&sec);
        if (tm) {
            snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03u +00:00",
                tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                tm->tm_hour, tm->tm_min, tm->tm_sec, millis);
        }

    } else if (t == MMS_BIT_STRING) {
        int nbits = MmsValue_getBitStringSize(val);
        /* §06: Quality (13-bit) decode */
        if (nbits >= 13 && (g_strcmp0(node->name, "q") == 0 ||
                            g_strcmp0(node->name, "subQ") == 0)) {
            format_quality(val, buf, sizeof(buf));
        }
        /* §02: Check (2-bit) */
        else if (nbits == 2 && g_strcmp0(node->name, "Check") == 0) {
            uint32_t bits = MmsValue_getBitStringAsInteger(val);
            int synchro   = (bits >> 1) & 1;
            int interlock = bits & 1;
            snprintf(buf, sizeof(buf), "%d%d", synchro, interlock);
        }
        /* Dbpos (2-bit coded enum for stVal/ctlVal/subVal) */
        else if (nbits == 2 && (g_strcmp0(node->name, "stVal") == 0 ||
                                g_strcmp0(node->name, "ctlVal") == 0 ||
                                g_strcmp0(node->name, "subVal") == 0)) {
            int dbpos = decodeDbpos(val);
            const char *label = NULL;
            switch (dbpos) {
                case 0: label = "intermediate-state"; break;
                case 1: label = "off (open)";         break;
                case 2: label = "on (closed)";        break;
                case 3: label = "bad-state";          break;
            }
            if (label)
                snprintf(buf, sizeof(buf), "%d (%s)", dbpos, label);
            else
                snprintf(buf, sizeof(buf), "%d", dbpos);
        } else {
            MmsValue_printToBuffer(val, buf, sizeof(buf));
        }

    } else if (t == MMS_OCTET_STRING) {
        /* Display as hex bytes */
        uint16_t len = MmsValue_getOctetStringSize(val);
        const uint8_t *data = MmsValue_getOctetStringBuffer(val);
        int pos = 0;
        for (uint16_t i = 0; i < len && pos < (int)sizeof(buf) - 3; i++)
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%02X", data[i]);
        if (len == 0) buf[0] = '\0';

    } else if (t == MMS_STRUCTURE) {
        MmsValue_printToBuffer(val, buf, sizeof(buf));
    } else {
        MmsValue_printToBuffer(val, buf, sizeof(buf));
    }

    g_free(node->value);
    node->value = g_strdup(buf);

    /* Resolve and set IEC 61850 BasicType */
    int bit_count = (t == MMS_BIT_STRING) ? MmsValue_getBitStringSize(val) : 0;
    const char *bt = resolve_basic_type(node->name, t, bit_count);
    if (bt) {
        g_free(node->basic_type);
        node->basic_type = g_strdup(bt);
    }
}

/* Find a child node by name */
static IedModelNode *
find_child_by_name(IedModelNode *parent, const char *name)
{
    if (!parent || !parent->children) return NULL;
    for (guint i = 0; i < parent->children->len; i++) {
        IedModelNode *c = g_ptr_array_index(parent->children, i);
        if (g_strcmp0(c->name, name) == 0) return c;
    }
    return NULL;
}

/*
 * Recursively map values onto a DA node and its sub-DA children.
 *
 * For structured values (e.g. Oper), read each sub-DA individually
 * by its full reference path.  This avoids the ordering mismatch
 * between IedConnection_getDataDirectory (alphabetical variable-list
 * order) and MMS structure elements (model-definition order), which
 * caused values such as Check, ctlVal, T, and Test to be swapped.
 *
 * For leaf values (non-structure), store the value directly.
 */
static void
map_mms_to_children(IedConnection con, IedModelNode *node,
                    MmsValue *val, const char *fc_name)
{
    if (!node) return;

    /* Tag the node with its FC */
    if (!node->fc && fc_name) node->fc = g_strdup(fc_name);

    /* If val is NULL (parent couldn't be read), still try to read
     * children individually — this handles the initial call case. */
    MmsType t = val ? MmsValue_getType(val) : MMS_DATA_ACCESS_ERROR;

    if (t == MMS_STRUCTURE && node->children && node->children->len > 0) {
        /* Store a summary on the structured node itself */
        store_mms_value(node, val);

        /* Read each child DA individually by its full reference path.
         * This guarantees correct value-to-node mapping regardless of
         * directory vs. structure ordering differences. */
        FunctionalConstraint fc = FunctionalConstraint_fromString(fc_name);

        for (guint i = 0; i < node->children->len; i++) {
            IedModelNode *child = g_ptr_array_index(node->children, i);
            IedClientError err;

            MmsValue *child_val = IedConnection_readObject(
                con, &err, child->ref, fc);

            if (child_val &&
                MmsValue_getType(child_val) != MMS_DATA_ACCESS_ERROR) {
                map_mms_to_children(con, child, child_val, fc_name);
            }

            if (child_val) MmsValue_delete(child_val);
        }
    } else if (val) {
        store_mms_value(node, val);
    }
}

/*
 * Read all DOs under one LN.
 *
 * For each DO, iterate over all functional constraints. For each FC,
 * read each DA individually by its full reference path. This avoids
 * the ordering mismatch between IedConnection_getDataDirectoryByFC
 * (which returns names in MMS variable-list / alphabetical order)
 * and IedConnection_readObject on the DO (which returns an MMS
 * structure in model-definition order, e.g. stVal, q, t for SPC).
 */
static void
read_ln_values(IedConnection con, IedModelNode *ln)
{
    for (guint di = 0; di < ln->children->len; di++) {
        IedModelNode *do_node = g_ptr_array_index(ln->children, di);
        if (do_node->type != NODE_DO) continue;

        /* Try each FC */
        for (int fi = 0; fi < NUM_READ_FCS; fi++) {
            IedClientError err;
            FunctionalConstraint fc = READ_FCS[fi];
            const char *fc_name = READ_FC_NAMES[fi];

            /* Get the DA names for this FC */
            LinkedList daNames = IedConnection_getDataDirectoryByFC(
                con, &err, do_node->ref, fc);
            if (!daNames) continue;

            /* Check if there are any DAs for this FC */
            LinkedList first = LinkedList_getNext(daNames);
            if (!first) {
                LinkedList_destroy(daNames);
                continue;
            }

            /* Read each DA individually by its full reference path.
             * This guarantees each value is assigned to the correct
             * node regardless of ordering differences. */
            LinkedList entry = first;
            while (entry) {
                const char *daName = (const char *)entry->data;
                IedModelNode *da = find_child_by_name(do_node, daName);

                if (da) {
                    MmsValue *val = IedConnection_readObject(
                        con, &err, da->ref, fc);

                    if (val && MmsValue_getType(val) != MMS_DATA_ACCESS_ERROR) {
                        g_print("  Read OK: %s [%s] type=%s\n",
                                da->ref, fc_name,
                                mms_type_name(MmsValue_getType(val)));
                        map_mms_to_children(con, da, val, fc_name);
                    }

                    if (val) MmsValue_delete(val);
                }

                entry = LinkedList_getNext(entry);
            }
            LinkedList_destroy(daNames);
        }
    }
}

static void
read_all_values_thread(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
    (void)source_object; (void)cancellable;
    AppState *app = (AppState *)task_data;

    /* 1) Poll SLD equipment array for telemetry */
    for (int i = 0; i < app->sld_equipment_count; i++) {
        SldEquipment *e = &app->sld_equipment[i];
        int bay_idx = e->bay_index;
        if (bay_idx < 0 || bay_idx >= app->sub_config.num_bays) continue;

        BayConnection *bay = &app->bays_con[bay_idx];
        if (!bay->connected || !bay->con) continue;

        IedClientError err;
        MmsValue *val = IedConnection_readObject(bay->con, &err, e->ref, IEC61850_FC_ST);
        if (err == IED_ERROR_OK && val) {
            int prev = e->state;
            e->state = decodeDbpos(val);
            if (prev != e->state)
                g_print("[SLD] %s : %d -> %d\n", e->ref, prev, e->state);
            MmsValue_delete(val);
        } else if (err != IED_ERROR_OK && bay->connected) {
            bay->connected = FALSE;
            g_print("[SLD] Connection lost for bay %d (%s:%d)\n", bay_idx, bay->host, bay->port);
        }
    }

    /* 2) Original GUI tree/model reading logic for the Data View */
    if (app->ui_model_root) {
        for (guint i = 0; i < app->ui_model_root->children->len; i++) {
            IedModelNode *ld = g_ptr_array_index(app->ui_model_root->children, i);
            for (guint j = 0; j < ld->children->len; j++) {
                IedModelNode *ln = g_ptr_array_index(ld->children, j);
                
                /* Only read what's currently in selection for auto_poll, or read everything if manual. */
                if (app->auto_poll && app->selected_ln != ln) {
                    continue;
                }
                
                if (!app->auto_poll) g_print("Reading: %s\n", ln->ref);
                
                if (app->explorer_connected && app->explorer_con) {
                    read_ln_values(app->explorer_con, ln);
                }
            }
        }
    }

    g_task_return_boolean(task, TRUE);
}

static void
read_all_values_finish(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    (void)source_object; (void)res;
    AppState *app = (AppState *)user_data;

    if (app->sld_area)
        gtk_widget_queue_draw(app->sld_area);

    /* Repopulate the data view only if we are doing a manual read.
       In auto_poll, GtkTickCallback will automatically fetch updated strings without collapsing items. */
    if (!app->auto_poll) {
        if (app->selected_ln)
            populate_data_view(app, app->selected_ln);

        set_status(app, "  Read all values complete", "status-connected");
        g_print("Read all values complete.\n");
    }
    
    if (app->col_view)
        gtk_widget_queue_draw(app->col_view);
}

void
app_read_all_values(AppState *app)
{
    if (!app->auto_poll) {
        g_print("Reading all values...\n");
        set_status(app, "  Reading all values…", "status-info");
    }

    GTask *task = g_task_new(NULL, NULL, read_all_values_finish, app);
    g_task_set_task_data(task, app, NULL);
    g_task_run_in_thread(task, read_all_values_thread);
    g_object_unref(task);
}

/* ------------------------------------------------------------------ */
/*  Write Feature — FC Gateway                                        */
/* ------------------------------------------------------------------ */

gboolean
is_node_writable(const char *fc)
{
    if (!fc) return FALSE;
    /* Hard non-writable per IEC 61850-7-2 §8 */
    if (g_strcmp0(fc, "ST") == 0) return FALSE;
    if (g_strcmp0(fc, "MX") == 0) return FALSE;
    if (g_strcmp0(fc, "DC") == 0) return FALSE;
    if (g_strcmp0(fc, "EX") == 0) return FALSE;
    if (g_strcmp0(fc, "OR") == 0) return FALSE;
    /* Writable FCs */
    if (g_strcmp0(fc, "SP") == 0) return TRUE;
    if (g_strcmp0(fc, "SV") == 0) return TRUE;
    if (g_strcmp0(fc, "CF") == 0) return TRUE;
    if (g_strcmp0(fc, "BR") == 0) return TRUE;
    if (g_strcmp0(fc, "RP") == 0) return TRUE;
    if (g_strcmp0(fc, "CO") == 0) return TRUE;
    /* Default: read-only */
    return FALSE;
}

/* ------------------------------------------------------------------ */
/*  Write Feature — Direct Write (SP/SV/CF) via GTask                 */
/* ------------------------------------------------------------------ */

typedef struct {
    AppState     *app;
    int           bay_index;
    char         *ref;
    char         *fc;
    MmsValue     *val;
    IedClientError err;
    gboolean      success;
} WriteTaskData;

static gboolean
write_result_idle(gpointer user_data)
{
    WriteTaskData *wd = (WriteTaskData *)user_data;
    if (wd->success) {
        char msg[512];
        snprintf(msg, sizeof(msg), "  Write OK: %s", wd->ref);
        set_status(wd->app, msg, "status-connected");
    } else {
        char msg[512];
        snprintf(msg, sizeof(msg), "  Write FAILED: %s (err %d)", wd->ref, wd->err);
        set_status(wd->app, msg, "status-disconnected");
    }
    g_free(wd->ref);
    g_free(wd->fc);
    if (wd->val) MmsValue_delete(wd->val);
    g_free(wd);
    return G_SOURCE_REMOVE;
}


/* Helper: format an MmsValue as a short string for logging */
static void
mms_value_to_log_str(MmsValue *val, char *buf, size_t buflen)
{
    if (!val) { snprintf(buf, buflen, "(null)"); return; }
    switch (MmsValue_getType(val)) {
    case MMS_BOOLEAN:
        snprintf(buf, buflen, "%s", MmsValue_getBoolean(val) ? "true" : "false");
        break;
    case MMS_INTEGER:
        snprintf(buf, buflen, "%d", MmsValue_toInt32(val));
        break;
    case MMS_UNSIGNED:
        snprintf(buf, buflen, "%u", MmsValue_toUint32(val));
        break;
    case MMS_FLOAT:
        snprintf(buf, buflen, "%.6f", MmsValue_toFloat(val));
        break;
    case MMS_VISIBLE_STRING:
    case MMS_STRING:
        snprintf(buf, buflen, "\"%s\"", MmsValue_toString(val));
        break;
    default:
        snprintf(buf, buflen, "(%s)", mms_type_name(MmsValue_getType(val)));
        break;
    }
}

static void
write_direct_thread(GTask *task, gpointer source, gpointer data, GCancellable *cancel);

/* Helper: Coerce MmsValue to the expected server type */
static MmsValue *
mms_value_coerce(MmsValue *writeVal, MmsType serverType)
{
    if (!writeVal || MmsValue_getType(writeVal) == serverType) {
        return writeVal; /* No conversion needed */
    }

    MmsValue *converted = NULL;
    switch (serverType) {
    case MMS_BOOLEAN:
        if (MmsValue_getType(writeVal) == MMS_INTEGER)
            converted = MmsValue_newBoolean(MmsValue_toInt32(writeVal) != 0);
        else if (MmsValue_getType(writeVal) == MMS_UNSIGNED)
            converted = MmsValue_newBoolean(MmsValue_toUint32(writeVal) != 0);
        else if (MmsValue_getType(writeVal) == MMS_FLOAT)
            converted = MmsValue_newBoolean(MmsValue_toFloat(writeVal) != 0.0f);
        else
            converted = MmsValue_newBoolean(MmsValue_toInt32(writeVal) != 0); /* Fallback */
        break;

    case MMS_INTEGER: {
        int32_t v = 0;
        if (MmsValue_getType(writeVal) == MMS_BOOLEAN)
            v = MmsValue_getBoolean(writeVal) ? 1 : 0;
        else if (MmsValue_getType(writeVal) == MMS_FLOAT)
            v = (int32_t)MmsValue_toFloat(writeVal);
        else if (MmsValue_getType(writeVal) == MMS_UNSIGNED)
            v = (int32_t)MmsValue_toUint32(writeVal);
        else
            v = MmsValue_toInt32(writeVal);
        converted = MmsValue_newIntegerFromInt32(v);
        break;
    }

    case MMS_UNSIGNED: {
        uint32_t v = 0;
        if (MmsValue_getType(writeVal) == MMS_BOOLEAN)
            v = MmsValue_getBoolean(writeVal) ? 1 : 0;
        else if (MmsValue_getType(writeVal) == MMS_INTEGER)
            v = (uint32_t)MmsValue_toInt32(writeVal);
        else if (MmsValue_getType(writeVal) == MMS_FLOAT)
            v = (uint32_t)MmsValue_toFloat(writeVal);
        else
            v = MmsValue_toUint32(writeVal);
        converted = MmsValue_newUnsignedFromUint32(v);
        break;
    }

    case MMS_FLOAT: {
        float v = 0;
        if (MmsValue_getType(writeVal) == MMS_BOOLEAN)
            v = MmsValue_getBoolean(writeVal) ? 1.0f : 0.0f;
        else if (MmsValue_getType(writeVal) == MMS_INTEGER)
            v = (float)MmsValue_toInt32(writeVal);
        else if (MmsValue_getType(writeVal) == MMS_UNSIGNED)
            v = (float)MmsValue_toUint32(writeVal);
        else
            v = MmsValue_toFloat(writeVal);
        converted = MmsValue_newFloat(v);
        break;
    }

    default:
        break;
    }

    if (converted) {
        g_print("  Convert : %s -> %s\n",
                mms_type_name(MmsValue_getType(writeVal)), mms_type_name(serverType));
        MmsValue_delete(writeVal);
        return converted;
    }
    return writeVal;
}

static void
write_direct_thread(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)task; (void)source; (void)cancel;
    WriteTaskData *wd = (WriteTaskData *)data;

    FunctionalConstraint fc = FunctionalConstraint_fromString(wd->fc);

    /* --- Log: begin write --- */
    g_print("\n========================================\n");
    g_print("  WRITE REQUEST\n");
    g_print("  Address : %s\n", wd->ref);
    g_print("  FC      : %s\n", wd->fc);
    {
        char vbuf[256];
        mms_value_to_log_str(wd->val, vbuf, sizeof(vbuf));
        g_print("  New Val : %s  (MmsType: %s)\n", vbuf, mms_type_name(MmsValue_getType(wd->val)));
    }
    g_print("----------------------------------------\n");

    /* --- Read the current value to determine the server's exact MMS type --- */
    IedConnection con = NULL;
    if (wd->bay_index >= 0 && wd->bay_index < wd->app->sub_config.num_bays) {
        con = wd->app->bays_con[wd->bay_index].con;
    } else {
        con = wd->app->explorer_con;
    }
    MmsValue *current = IedConnection_readObject(con, &wd->err, wd->ref, fc);
    if (!current || wd->err != IED_ERROR_OK) {
        g_print("  FAILED  : could not read current value (err %d)\n", wd->err);
        g_print("========================================\n\n");
        wd->success = FALSE;
        if (current) MmsValue_delete(current);
        g_idle_add(write_result_idle, wd);
        return;
    }

    MmsType serverType = MmsValue_getType(current);
    {
        char cbuf[256];
        mms_value_to_log_str(current, cbuf, sizeof(cbuf));
        g_print("  Cur Val : %s  (Server MmsType: %s)\n", cbuf, mms_type_name(serverType));
    }

    MmsValue *writeVal = mms_value_coerce(wd->val, serverType);
    wd->val = writeVal;

    MmsValue_delete(current);

    /* --- Now write with the correctly typed value --- */
    IedConnection_writeObject(con, &wd->err, wd->ref, fc, writeVal);
    wd->success = (wd->err == IED_ERROR_OK);

    {
        char wbuf[256];
        mms_value_to_log_str(writeVal, wbuf, sizeof(wbuf));
        g_print("  Sent    : %s  (MmsType: %s)\n", wbuf, mms_type_name(MmsValue_getType(writeVal)));
    }
    g_print("  Result  : %s  (err=%d)\n", wd->success ? "SUCCESS" : "FAILED", wd->err);
    g_print("========================================\n\n");

    g_idle_add(write_result_idle, wd);
}

void
app_write_direct(AppState *app, int bay_index, IedModelNode *da, MmsValue *val)
{
    gboolean is_connected = FALSE;
    if (bay_index >= 0 && bay_index < app->sub_config.num_bays) {
        is_connected = app->bays_con[bay_index].connected;
    } else {
        is_connected = app->explorer_connected;
    }

    if (!is_connected || !da || !val) return;
    if (!da->fc || !is_node_writable(da->fc)) return;
    /* Never write CO via SetDataValues */
    if (g_strcmp0(da->fc, "CO") == 0) return;

    WriteTaskData *wd = g_new0(WriteTaskData, 1);
    wd->app = app;
    wd->bay_index = bay_index;
    wd->ref = g_strdup(da->ref);
    wd->fc  = g_strdup(da->fc);
    wd->val = val; /* ownership transferred */
    wd->err = IED_ERROR_OK;

    set_status(app, "  Writing…", "status-info");

    GTask *task = g_task_new(NULL, NULL, NULL, NULL);
    g_task_set_task_data(task, wd, NULL);
    g_task_run_in_thread(task, write_direct_thread);
    g_object_unref(task);
}

/* ------------------------------------------------------------------ */
/*  Write Feature — Control Operate (CO) via ControlObjectClient      */
/* ------------------------------------------------------------------ */

typedef struct {
    AppState           *app;
    int                 bay_index;
    char               *doRef;
    MmsValue           *ctlVal;
    int                 ctlModel;
    ControlAction       action;
    gboolean            success;
    IedClientError      err;
} ControlTaskData;

/* One-shot timer: re-read SLD telemetry after a short delay
 * so the server has time to settle and the control callback
 * chain has fully completed. */
static gboolean
deferred_sld_refresh(gpointer user_data)
{
    AppState *app = (AppState *)user_data;
    app_read_all_values(app);
    return G_SOURCE_REMOVE;
}

static gboolean
control_result_idle(gpointer user_data)
{
    ControlTaskData *cd = (ControlTaskData *)user_data;
    if (cd->success) {
        char msg[512];
        if (cd->action == CTL_ACTION_SELECT) {
            snprintf(msg, sizeof(msg), "  Select OK (Ready to Operate): %s", cd->doRef);
        } else if (cd->action == CTL_ACTION_CANCEL) {
            snprintf(msg, sizeof(msg), "  Control Cancelled: %s", cd->doRef);
        } else {
            snprintf(msg, sizeof(msg), "  Control OK: %s", cd->doRef);
        }
        set_status(cd->app, msg, "status-connected");
        /* Automatically trigger a deferred active read 300ms after control
         * so 3rd-party servers without configured URCB reports will still have their
         * updated stVal reflected on the SLD. */
        g_timeout_add(300, deferred_sld_refresh, cd->app);
    } else {
        char msg[512];
        snprintf(msg, sizeof(msg), "  Control FAILED: %s (err %d)", cd->doRef, cd->err);
        set_status(cd->app, msg, "status-disconnected");
    }
    g_free(cd->doRef);
    if (cd->ctlVal) MmsValue_delete(cd->ctlVal);
    g_free(cd);
    return G_SOURCE_REMOVE;
}

static void commandTerminationHandler(void *parameter, ControlObjectClient connection)
{
    LastApplError lastApplError = ControlObjectClient_getLastApplError(connection);
    if (lastApplError.error != 0) {
        g_print("\n[CALLBACK] CommandTermination- (Execution Failed).\n");
        g_print(" -> LastApplError: %i, addCause: %i\n", lastApplError.error, lastApplError.addCause);
    } else {
        g_print("\n[CALLBACK] CommandTermination+ (Execution Fully Confirmed).\n");
    }
}

static void
control_operate_thread(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)task; (void)source; (void)cancel;
    ControlTaskData *cd = (ControlTaskData *)data;
    IedConnection con = NULL;
    if (cd->bay_index >= 0 && cd->bay_index < cd->app->sub_config.num_bays) {
        con = cd->app->bays_con[cd->bay_index].con;
    } else {
        con = cd->app->explorer_con;
    }

    g_print("[CONTROL] Initiating control for %s (Action: %d)...\n", cd->doRef, cd->action);

    g_mutex_lock(&g_active_session_mutex);
    ControlObjectClient ctlObj = NULL;

    if (cd->action == CTL_ACTION_OPERATE) {
        if (g_active_session.ctlObj && g_active_session.con == con &&
            g_strcmp0(g_active_session.doRef, cd->doRef) == 0) {
            gint64 now = g_get_monotonic_time() / 1000;
            if ((now - g_active_session.select_time_ms) < 30000) {
                ctlObj = g_active_session.ctlObj;
                g_active_session.ctlObj = NULL; /* Take ownership for Operate */
                g_print("[CONTROL] Reusing active SBO control session for %s\n", cd->doRef);
            } else {
                g_print("[CONTROL] Active SBO session expired (>30s) for %s. Discarding.\n", cd->doRef);
                ControlObjectClient_destroy(g_active_session.ctlObj);
                g_active_session.ctlObj = NULL;
            }
        }
    } else if (cd->action == CTL_ACTION_CANCEL) {
        if (g_active_session.ctlObj && g_active_session.con == con &&
            g_strcmp0(g_active_session.doRef, cd->doRef) == 0) {
            ctlObj = g_active_session.ctlObj;
            g_active_session.ctlObj = NULL;
        }
        if (ctlObj) {
            g_print("[CONTROL] Cancelling active SBO selection for %s...\n", cd->doRef);
            ControlObjectClient_cancel(ctlObj);
            ControlObjectClient_destroy(ctlObj);
        }
        g_mutex_unlock(&g_active_session_mutex);
        cd->success = TRUE;
        cd->err = IED_ERROR_OK;
        g_idle_add(control_result_idle, cd);
        return;
    } else {
        /* When initiating SELECT or SEL_OP, clear any prior active session */
        if (g_active_session.ctlObj) {
            ControlObjectClient_destroy(g_active_session.ctlObj);
            g_active_session.ctlObj = NULL;
        }
    }
    g_mutex_unlock(&g_active_session_mutex);

    if (!ctlObj) {
        ctlObj = ControlObjectClient_create(cd->doRef, con);
        if (!ctlObj) {
            g_print("[CONTROL ERROR] Failed to create ControlObjectClient for %s\n", cd->doRef);
            cd->success = FALSE;
            cd->err = IED_ERROR_OBJECT_DOES_NOT_EXIST;
            g_idle_add(control_result_idle, cd);
            return;
        }
        ControlObjectClient_setOrigin(ctlObj, "IEC61850-GTK-CLIENT", CONTROL_ORCAT_REMOTE_CONTROL);
        ControlObjectClient_setCommandTerminationHandler(ctlObj, commandTerminationHandler, NULL);
        ControlObjectClient_setInterlockCheck(ctlObj, false);
        ControlObjectClient_setSynchroCheck(ctlObj, false);
    }

    /* --- Log: begin control --- */
    g_print("\n========================================\n");
    g_print("  CONTROL REQUEST\n");
    g_print("  DO Ref  : %s\n", cd->doRef);
    {
        char vbuf[256];
        mms_value_to_log_str(cd->ctlVal, vbuf, sizeof(vbuf));
        g_print("  New Val : %s  (UI MmsType: %s)\n", vbuf, mms_type_name(MmsValue_getType(cd->ctlVal)));
    }
    g_print("----------------------------------------\n");

    /* Type Coercion */
    MmsType expectedType = ControlObjectClient_getCtlValType(ctlObj);
    g_print("  Server Expected ctlVal MmsType: %s\n", mms_type_name(expectedType));

    MmsValue *writeVal = mms_value_coerce(cd->ctlVal, expectedType);
    if (writeVal != cd->ctlVal) {
        MmsValue_delete(cd->ctlVal);
        cd->ctlVal = writeVal;
    }

    /* Dynamically detect the server's actual control model.
     * ControlObjectClient_create already read <doRef>.ctlModel[CF] from
     * the server, so this value is authoritative and overrides whatever
     * the GUI layer passed in cd->ctlModel. */
    ControlModel actualModel = ControlObjectClient_getControlModel(ctlObj);
    g_print("  Server ctlModel : %d", actualModel);
    switch (actualModel) {
        case CONTROL_MODEL_STATUS_ONLY:      g_print(" (status-only)\n");        break;
        case CONTROL_MODEL_DIRECT_NORMAL:    g_print(" (direct-normal)\n");      break;
        case CONTROL_MODEL_SBO_NORMAL:       g_print(" (SBO-normal)\n");         break;
        case CONTROL_MODEL_DIRECT_ENHANCED:  g_print(" (direct-enhanced)\n");    break;
        case CONTROL_MODEL_SBO_ENHANCED:     g_print(" (SBO-enhanced)\n");       break;
        default:                             g_print(" (unknown)\n");             break;
    }

    if (actualModel == CONTROL_MODEL_SBO_NORMAL || actualModel == CONTROL_MODEL_SBO_ENHANCED) {
        if (cd->action == CTL_ACTION_SELECT) {
            bool selOk = (actualModel == CONTROL_MODEL_SBO_NORMAL) ? 
                         ControlObjectClient_select(ctlObj) : 
                         ControlObjectClient_selectWithValue(ctlObj, cd->ctlVal);
            g_print("  Select : %s\n", selOk ? "OK" : "FAILED");
            cd->success = selOk;
            cd->err = ControlObjectClient_getLastError(ctlObj);
        } else if (cd->action == CTL_ACTION_OPERATE) {
            cd->success = ControlObjectClient_operate(ctlObj, cd->ctlVal, 0);
            cd->err = ControlObjectClient_getLastError(ctlObj);
        } else if (cd->action == CTL_ACTION_SEL_OP) {
            bool selOk = (actualModel == CONTROL_MODEL_SBO_NORMAL) ? 
                         ControlObjectClient_select(ctlObj) : 
                         ControlObjectClient_selectWithValue(ctlObj, cd->ctlVal);
            g_print("  Select : %s\n", selOk ? "OK" : "FAILED");
            if (selOk) {
                cd->success = ControlObjectClient_operate(ctlObj, cd->ctlVal, 0);
            } else {
                cd->success = FALSE;
            }
            cd->err = ControlObjectClient_getLastError(ctlObj);
        }
    } else {
        /* Direct operate — ctlModel 1, 3, or any unrecognised value */
        cd->success = ControlObjectClient_operate(ctlObj, cd->ctlVal, 0);
        cd->err = ControlObjectClient_getLastError(ctlObj);
    }

    if (cd->action == CTL_ACTION_SELECT && cd->success) {
        /* Preserve the selected ControlObjectClient so that subsequent Operate matches */
        g_mutex_lock(&g_active_session_mutex);
        if (g_active_session.ctlObj) {
            ControlObjectClient_destroy(g_active_session.ctlObj);
        }
        g_active_session.con = con;
        g_strlcpy(g_active_session.doRef, cd->doRef, sizeof(g_active_session.doRef));
        g_active_session.ctlObj = ctlObj;
        g_active_session.select_time_ms = g_get_monotonic_time() / 1000;
        g_mutex_unlock(&g_active_session_mutex);
        g_print("[CONTROL] SBO Select successful. Session kept alive for subsequent Operate.\n");
    } else {
        /* Wait for CommandTermination callback before destroying */
        Thread_sleep(1000);
        ControlObjectClient_destroy(ctlObj);
    }

    {
        char wbuf[256];
        mms_value_to_log_str(cd->ctlVal, wbuf, sizeof(wbuf));
        g_print("  Sent    : %s  (MmsType: %s)\n", wbuf, mms_type_name(MmsValue_getType(cd->ctlVal)));
    }
    g_print("  Result  : %s  (err=%d)\n", cd->success ? "SUCCESS" : "FAILED", cd->err);
    g_print("========================================\n\n");

    g_idle_add(control_result_idle, cd);
}

void
app_control_operate(AppState *app, int bay_index, const char *doRef, MmsValue *ctlVal, int ctlModel, ControlAction action)
{
    gboolean is_connected = FALSE;
    if (bay_index >= 0 && bay_index < app->sub_config.num_bays) {
        is_connected = app->bays_con[bay_index].connected;
    } else {
        is_connected = app->explorer_connected;
    }

    if (!is_connected || !doRef || !ctlVal) return;

    ControlTaskData *cd = g_new0(ControlTaskData, 1);
    cd->app      = app;
    cd->bay_index = bay_index;
    cd->doRef    = g_strdup(doRef);
    cd->ctlVal   = ctlVal; /* ownership transferred */
    cd->ctlModel = ctlModel;
    cd->action   = action;

    set_status(app, "  Sending control command…", "status-info");

    GTask *task = g_task_new(NULL, NULL, NULL, NULL);
    g_task_set_task_data(task, cd, NULL);
    g_task_run_in_thread(task, control_operate_thread);
    g_object_unref(task);
}

void
app_control_cancel(AppState *app, int bay_index, const char *doRef)
{
    gboolean is_connected = FALSE;
    if (bay_index >= 0 && bay_index < app->sub_config.num_bays) {
        is_connected = app->bays_con[bay_index].connected;
    } else {
        is_connected = app->explorer_connected;
    }

    if (!is_connected || !doRef) return;

    ControlTaskData *cd = g_new0(ControlTaskData, 1);
    cd->app       = app;
    cd->bay_index = bay_index;
    cd->doRef     = g_strdup(doRef);
    cd->ctlVal    = NULL;
    cd->ctlModel  = 0;
    cd->action    = CTL_ACTION_CANCEL;

    set_status(app, "  Cancelling control...", "status-info");

    GTask *task = g_task_new(NULL, NULL, NULL, NULL);
    g_task_set_task_data(task, cd, NULL);
    g_task_run_in_thread(task, control_operate_thread);
    g_object_unref(task);
}
