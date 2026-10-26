#include "iec61850_app.h"
#include "substation_config.h"
#include <stdio.h>
#include <string.h>

/* ================================================================== */
/*  IecTreeItem GObject (left pane — tree explorer)                   */
/* ================================================================== */

struct _IecTreeItemClass { GObjectClass parent_class; };
G_DEFINE_TYPE(IecTreeItem, iec_tree_item, G_TYPE_OBJECT)

static void iec_tree_item_finalize(GObject *obj) {
    IecTreeItem *i = IEC_TREE_ITEM(obj);
    g_free(i->name); g_free(i->icon);
    G_OBJECT_CLASS(iec_tree_item_parent_class)->finalize(obj);
}
static void iec_tree_item_class_init(IecTreeItemClass *k) {
    G_OBJECT_CLASS(k)->finalize = iec_tree_item_finalize;
}
static void iec_tree_item_init(IecTreeItem *s) { (void)s; }

static IecTreeItem *
iec_tree_item_new(const char *name, const char *icon, NodeType nt, IedModelNode *mn) {
    IecTreeItem *i = g_object_new(IEC_TYPE_TREE_ITEM, NULL);
    i->name = g_strdup(name); i->icon = g_strdup(icon);
    i->node_type = nt; i->model_node = mn;
    return i;
}

/* ================================================================== */
/*  IecRowItem GObject (right pane — data attribute table)            */
/* ================================================================== */

struct _IecRowItemClass { GObjectClass parent_class; };
G_DEFINE_TYPE(IecRowItem, iec_row_item, G_TYPE_OBJECT)

static void iec_row_item_finalize(GObject *obj) {
    IecRowItem *i = IEC_ROW_ITEM(obj);
    g_free(i->name); g_free(i->value); g_free(i->type_str);
    g_free(i->mms_type); g_free(i->timestamp); g_free(i->description);
    g_free(i->fc); g_free(i->badge_type);
    G_OBJECT_CLASS(iec_row_item_parent_class)->finalize(obj);
}
static void iec_row_item_class_init(IecRowItemClass *k) {
    G_OBJECT_CLASS(k)->finalize = iec_row_item_finalize;
}
static void iec_row_item_init(IecRowItem *s) { (void)s; }

static IecRowItem *
iec_row_item_new(IedModelNode *mn) {
    IecRowItem *i = g_object_new(IEC_TYPE_ROW_ITEM, NULL);
    i->name        = g_strdup(mn->name);
    i->value       = g_strdup(mn->value       ? mn->value       : "");
    i->type_str    = g_strdup(mn->description  ? mn->description : "");
    i->mms_type    = g_strdup(mn->mms_type     ? mn->mms_type    : "");
    i->timestamp   = g_strdup(mn->timestamp    ? mn->timestamp   : "");
    i->description = g_strdup(mn->description  ? mn->description : "");
    i->fc          = g_strdup(mn->fc           ? mn->fc          : "");
    i->badge_type  = g_strdup(mn->type == NODE_DO ? "DO" : "DA");
    i->node_type   = mn->type;
    i->model_node  = mn;
    return i;
}

/* ================================================================== */
/*  Helper functions                                                  */
/* ================================================================== */

static const char *node_type_badge_css(NodeType t) {
    switch (t) {
        case NODE_DO: return "badge-do";
        case NODE_DA: return "badge-da";
        default:      return "";
    }
}

static const char *fc_badge_css(const char *fc) {
    if (!fc || !*fc) return "badge-fc";
    if (g_strcmp0(fc, "MX") == 0) return "badge-mx";
    if (g_strcmp0(fc, "ST") == 0) return "badge-st";
    if (g_strcmp0(fc, "CF") == 0) return "badge-cf";
    if (g_strcmp0(fc, "CO") == 0) return "badge-co";
    if (g_strcmp0(fc, "DC") == 0) return "badge-dc";
    return "badge-fc";
}

/* ================================================================== */
/*  Status bar                                                        */
/* ================================================================== */

void set_status(AppState *app, const char *msg, const char *css_class) {
    gtk_label_set_text(GTK_LABEL(app->status_label), msg);
    GtkWidget *w = app->status_label;
    gtk_widget_remove_css_class(w, "status-connected");
    gtk_widget_remove_css_class(w, "status-disconnected");
    gtk_widget_remove_css_class(w, "status-info");
    if (css_class) gtk_widget_add_css_class(w, css_class);
}

/* ================================================================== */
/*  Populate left pane — IED explorer tree                            */
/* ================================================================== */

static void add_tree_children(GListStore *store, IedModelNode *parent);

static GListModel *
tree_children_func(gpointer item, gpointer user_data)
{
    (void)user_data;
    IecTreeItem *ti = IEC_TREE_ITEM(item);
    IedModelNode *mn = ti->model_node;
    if (!mn || !mn->children || mn->children->len == 0) return NULL;

    /* Only show LD and LN levels in the explorer */
    gboolean has_explorable = FALSE;
    for (guint i = 0; i < mn->children->len; i++) {
        IedModelNode *c = g_ptr_array_index(mn->children, i);
        if (c->type == NODE_LD || c->type == NODE_LN) { has_explorable = TRUE; break; }
    }
    if (!has_explorable) return NULL;

    GListStore *child_store = g_list_store_new(IEC_TYPE_TREE_ITEM);
    for (guint i = 0; i < mn->children->len; i++) {
        IedModelNode *c = g_ptr_array_index(mn->children, i);
        if (c->type != NODE_LD && c->type != NODE_LN) continue;
        const char *icon = (c->type == NODE_LD) ? "drive-harddisk-symbolic" : "folder-symbolic";
        IecTreeItem *ci = iec_tree_item_new(c->name, icon, c->type, c);
        g_list_store_append(child_store, ci);
        g_object_unref(ci);
    }
    return G_LIST_MODEL(child_store);
}

void populate_tree_view(AppState *app) {
    g_list_store_remove_all(app->tree_store);
    if (!app->ui_model_root) return;

    for (guint i = 0; i < app->ui_model_root->children->len; i++) {
        IedModelNode *ld = g_ptr_array_index(app->ui_model_root->children, i);
        IecTreeItem *item = iec_tree_item_new(ld->name, "drive-harddisk-symbolic", NODE_LD, ld);
        g_list_store_append(app->tree_store, item);
        g_object_unref(item);
    }
}

/* ================================================================== */
/*  Populate right pane — Data Objects / Attributes for selected LN   */
/* ================================================================== */

static void
add_do_children_recursive(GListStore *store, IedModelNode *parent)
{
    for (guint i = 0; i < parent->children->len; i++) {
        IedModelNode *child = g_ptr_array_index(parent->children, i);
        IecRowItem *ri = iec_row_item_new(child);
        g_list_store_append(store, ri);
        g_object_unref(ri);
    }
}

static GListModel *
data_children_func(gpointer item, gpointer user_data)
{
    (void)user_data;
    IecRowItem *ri = IEC_ROW_ITEM(item);
    IedModelNode *mn = ri->model_node;
    if (!mn || !mn->children || mn->children->len == 0) return NULL;

    GListStore *child_store = g_list_store_new(IEC_TYPE_ROW_ITEM);
    add_do_children_recursive(child_store, mn);
    return G_LIST_MODEL(child_store);
}

void populate_data_view(AppState *app, IedModelNode *ln_node) {
    app->selected_ln = ln_node;
    g_list_store_remove_all(app->data_store);

    if (!ln_node) return;

    /* Discover child DOs and DAs on demand if not yet discovered */
    if (ln_node->children->len == 0 && app->explorer_connected && app->explorer_con) {
        discover_ln_children(app->explorer_con, ln_node);
    }

    /* Update breadcrumb */
    char path[512];
    snprintf(path, sizeof(path), "  %s", ln_node->ref);
    gtk_label_set_text(GTK_LABEL(app->path_label), path);

    /* Add DOs under this LN */
    for (guint i = 0; i < ln_node->children->len; i++) {
        IedModelNode *dobj = g_ptr_array_index(ln_node->children, i);
        IecRowItem *ri = iec_row_item_new(dobj);
        g_list_store_append(app->data_store, ri);
        g_object_unref(ri);
    }
}

/* ================================================================== */
/*  Tree explorer — selection callback                                */
/* ================================================================== */

static void
on_tree_selection_changed(GtkSelectionModel *sel, guint pos, guint n, gpointer user_data)
{
    (void)pos; (void)n;
    AppState *app = (AppState *)user_data;

    GtkBitset *bs = gtk_selection_model_get_selection(sel);
    if (gtk_bitset_is_empty(bs)) return;
    guint idx = gtk_bitset_get_nth(bs, 0);

    GtkTreeListRow *row = GTK_TREE_LIST_ROW(
        g_list_model_get_item(G_LIST_MODEL(sel), idx));
    if (!row) return;

    IecTreeItem *ti = IEC_TREE_ITEM(gtk_tree_list_row_get_item(row));
    g_object_unref(row);
    if (!ti) return;

    /* If it's an LN, populate right pane */
    if (ti->node_type == NODE_LN) {
        populate_data_view(app, ti->model_node);
    } else if (ti->node_type == NODE_LD) {
        /* Show first LN of this LD, or clear */
        IedModelNode *ld = ti->model_node;
        if (ld->children->len > 0) {
            IedModelNode *first_ln = g_ptr_array_index(ld->children, 0);
            if (first_ln->type == NODE_LN)
                populate_data_view(app, first_ln);
        }
    }
    g_object_unref(ti);
}

/* ================================================================== */
/*  Signal handlers                                                   */
/* ================================================================== */

static void
on_connect_finish(GObject *source, GAsyncResult *res, gpointer user_data)
{
    (void)source;
    AppState *app = (AppState *)user_data;
    ConnectTaskData *cd = (ConnectTaskData *)g_task_get_task_data(G_TASK(res));

    gtk_widget_set_sensitive(app->connect_btn, TRUE);

    if (cd && cd->success && app->explorer_connected) {
        IedModelNode *model = cd->model;

        if (!app->ui_model_root) {
            app->ui_model_root = ied_model_node_new("IED", "", NODE_ROOT);
        } else {
            g_ptr_array_set_size(app->ui_model_root->children, 0);
        }

        if (model && model->children->len > 0) {
            for (guint c = 0; c < model->children->len; c++) {
                IedModelNode *ld = g_ptr_array_index(model->children, c);
                g_ptr_array_add(app->ui_model_root->children, ld);
            }
            g_free(model->name);
            g_free(model->ref);
            g_ptr_array_free(model->children, FALSE);
            g_free(model);
        } else if (model) {
            ied_model_node_free(model);
        }

        gtk_button_set_label(GTK_BUTTON(app->connect_btn), "  Disconnect");
        gtk_widget_add_css_class(app->connect_btn, "destructive-action");

        populate_tree_view(app);

        /* Auto-select first LN of first LD if available */
        if (app->ui_model_root && app->ui_model_root->children->len > 0) {
            IedModelNode *first_ld = g_ptr_array_index(app->ui_model_root->children, 0);
            if (first_ld->children->len > 0) {
                IedModelNode *first_ln = g_ptr_array_index(first_ld->children, 0);
                populate_data_view(app, first_ln);
            }
        }

        /* Automatically enable auto poll upon successful connection */
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->auto_poll_btn), TRUE);

        char msg[256];
        snprintf(msg, sizeof(msg), "  Connected to %s:%d", cd->host, cd->port);
        set_status(app, msg, "status-connected");
    } else {
        gtk_button_set_label(GTK_BUTTON(app->connect_btn), "  Connect");
        gtk_widget_remove_css_class(app->connect_btn, "destructive-action");
        set_status(app, "  Connection failed", "status-disconnected");
    }

    if (app->sld_area)
        gtk_widget_queue_draw(app->sld_area);
}

static void on_connect_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AppState *app = (AppState *)user_data;

    if (app->explorer_connected) {
        /* Turn off auto poll on disconnect */
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->auto_poll_btn), FALSE);
        
        app_disconnect(app);
        sld_disconnect_bays(app);
        gtk_button_set_label(GTK_BUTTON(app->connect_btn), "  Connect");
        gtk_widget_remove_css_class(app->connect_btn, "destructive-action");
        set_status(app, "Disconnected", "status-disconnected");
        if (app->sld_area)
            gtk_widget_queue_draw(app->sld_area);
        return;
    }

    const char *host = gtk_editable_get_text(GTK_EDITABLE(app->host_entry));
    int port = atoi(gtk_editable_get_text(GTK_EDITABLE(app->port_entry)));

    gtk_widget_set_sensitive(app->connect_btn, FALSE);
    gtk_button_set_label(GTK_BUTTON(app->connect_btn), "  Connecting...");
    set_status(app, "  Connecting...", "status-info");

    app_connect_async(app, host, port, on_connect_finish, app);
}

static void on_read_all_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AppState *app = (AppState *)user_data;
    app_read_all_values(app);
}

static gboolean auto_poll_tick(gpointer user_data) {
    AppState *app = (AppState *)user_data;
    if (app->auto_poll) {
        app_read_all_values(app);
    }
    return G_SOURCE_CONTINUE;
}

static void on_auto_poll_toggled(GtkToggleButton *btn, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    app->auto_poll = gtk_toggle_button_get_active(btn);
    if (app->auto_poll) {
        if (app->poll_timer_id == 0) {
            app->poll_timer_id = g_timeout_add(2500, auto_poll_tick, app);
        }
    } else {
        if (app->poll_timer_id != 0) {
            g_source_remove(app->poll_timer_id);
            app->poll_timer_id = 0;
        }
    }
}

/* ================================================================== */
/*  Write Feature — Data row selection + Dialog                       */
/* ================================================================== */

static void
on_data_selection_changed(GtkSelectionModel *sel, guint pos, guint n, gpointer user_data)
{
    (void)pos; (void)n;
    AppState *app = (AppState *)user_data;
    app->selected_da = NULL;

    GtkBitset *bs = gtk_selection_model_get_selection(sel);
    if (gtk_bitset_is_empty(bs)) {
        gtk_widget_set_sensitive(app->write_btn, FALSE);
        gtk_widget_set_tooltip_text(app->write_btn, "Select a Data Attribute");
        return;
    }
    guint idx = gtk_bitset_get_nth(bs, 0);
    GtkTreeListRow *row = GTK_TREE_LIST_ROW(
        g_list_model_get_item(G_LIST_MODEL(sel), idx));
    if (!row) return;

    IecRowItem *ri = IEC_ROW_ITEM(gtk_tree_list_row_get_item(row));
    g_object_unref(row);
    if (!ri || !ri->model_node) return;

    app->selected_da = ri->model_node;
    const char *fc = ri->model_node->fc;

    if (fc && is_node_writable(fc)) {
        gtk_widget_set_sensitive(app->write_btn, TRUE);
        if (g_strcmp0(fc, "CO") == 0)
            gtk_widget_set_tooltip_text(app->write_btn, "Control (FC=CO)");
        else {
            char tip[64];
            snprintf(tip, sizeof(tip), "Write Value (FC=%s)", fc);
            gtk_widget_set_tooltip_text(app->write_btn, tip);
        }
    } else {
        gtk_widget_set_sensitive(app->write_btn, FALSE);
        char tip[64];
        snprintf(tip, sizeof(tip), "Read Only (FC=%s)", fc ? fc : "?");
        gtk_widget_set_tooltip_text(app->write_btn, tip);
    }
    g_object_unref(ri);
}

/* ----- Dialog context structs (file scope for C callbacks) ----- */

typedef struct {
    AppState     *app;
    int           bay_index;
    IedModelNode *da;
    GtkWidget    *input;
    GtkWidget    *dialog;
    gboolean      is_bool;
    gboolean      is_float;
    gboolean      is_int;
} WriteDialogCtx;

static void write_dialog_ctx_free(gpointer data) { g_free(data); }

static void
on_direct_write_confirmed(GtkButton *btn, gpointer ud)
{
    (void)btn;
    WriteDialogCtx *c = (WriteDialogCtx *)ud;
    MmsValue *val = NULL;
    if (c->is_bool) {
        val = MmsValue_newBoolean(gtk_switch_get_active(GTK_SWITCH(c->input)));
    } else if (c->is_float) {
        val = MmsValue_newFloat(gtk_spin_button_get_value(GTK_SPIN_BUTTON(c->input)));
    } else if (c->is_int) {
        val = MmsValue_newIntegerFromInt32((int32_t)gtk_spin_button_get_value(GTK_SPIN_BUTTON(c->input)));
    } else {
        const char *txt = gtk_editable_get_text(GTK_EDITABLE(c->input));
        val = MmsValue_newVisibleString(txt);
    }
    app_write_direct(c->app, c->bay_index, c->da, val);
    gtk_window_destroy(GTK_WINDOW(c->dialog));
}

typedef struct {
    AppState  *app;
    int        bay_index;
    char       doRef[512];
    GtkWidget *ctl_switch;
    GtkWidget *dialog;
} CtlDialogCtx;

static void ctl_dialog_ctx_free(gpointer data) { g_free(data); }

static void
on_control_cancel_confirmed(GtkButton *btn, gpointer ud)
{
    (void)btn;
    CtlDialogCtx *c = (CtlDialogCtx *)ud;
    app_control_cancel(c->app, c->bay_index, c->doRef);
    gtk_window_destroy(GTK_WINDOW(c->dialog));
}

static void
on_control_select_confirmed(GtkButton *btn, gpointer ud)
{
    (void)btn;
    CtlDialogCtx *c = (CtlDialogCtx *)ud;
    MmsValue *ctlVal = MmsValue_newBoolean(
        gtk_switch_get_active(GTK_SWITCH(c->ctl_switch)));
    app_control_operate(c->app, c->bay_index, c->doRef, ctlVal, 1, CTL_ACTION_SELECT);
}

static void
on_control_operate_confirmed(GtkButton *btn, gpointer ud)
{
    (void)btn;
    CtlDialogCtx *c = (CtlDialogCtx *)ud;
    MmsValue *ctlVal = MmsValue_newBoolean(
        gtk_switch_get_active(GTK_SWITCH(c->ctl_switch)));
    app_control_operate(c->app, c->bay_index, c->doRef, ctlVal, 1, CTL_ACTION_OPERATE);
}

static void
on_control_sel_op_confirmed(GtkButton *btn, gpointer ud)
{
    (void)btn;
    CtlDialogCtx *c = (CtlDialogCtx *)ud;
    MmsValue *ctlVal = MmsValue_newBoolean(
        gtk_switch_get_active(GTK_SWITCH(c->ctl_switch)));
    app_control_operate(c->app, c->bay_index, c->doRef, ctlVal, 1, CTL_ACTION_SEL_OP);
}

/* ----- Direct Write Dialog (SP/SV/CF) ----- */

void
show_direct_write_dialog(AppState *app, int bay_index, IedModelNode *da)
{
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Direct Write");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 360, 200);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(app->window));

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_window_set_child(GTK_WINDOW(dialog), box);

    /* Reference label */
    char ref_txt[512];
    snprintf(ref_txt, sizeof(ref_txt), "DA: %s  [FC=%s]", da->ref, da->fc ? da->fc : "?");
    GtkWidget *lbl_ref = gtk_label_new(ref_txt);
    gtk_label_set_xalign(GTK_LABEL(lbl_ref), 0);
    gtk_widget_add_css_class(lbl_ref, "breadcrumb");
    gtk_box_append(GTK_BOX(box), lbl_ref);

    /* Type info */
    char type_txt[256];
    snprintf(type_txt, sizeof(type_txt), "Type: %s / %s",
             da->basic_type ? da->basic_type : "Unknown",
             da->mms_type ? da->mms_type : "Unknown");
    GtkWidget *lbl_type = gtk_label_new(type_txt);
    gtk_label_set_xalign(GTK_LABEL(lbl_type), 0);
    gtk_box_append(GTK_BOX(box), lbl_type);

    /* Current value */
    char cur_txt[256];
    snprintf(cur_txt, sizeof(cur_txt), "Current: %s", da->value ? da->value : "(empty)");
    GtkWidget *lbl_cur = gtk_label_new(cur_txt);
    gtk_label_set_xalign(GTK_LABEL(lbl_cur), 0);
    gtk_box_append(GTK_BOX(box), lbl_cur);

    /* Input widget — choose by basic_type */
    GtkWidget *input = NULL;
    const char *bt = da->basic_type;
    gboolean is_bool = (bt && (g_strcmp0(bt, "BOOLEAN") == 0));
    gboolean is_float = (bt && (g_strcmp0(bt, "FLOAT32") == 0 || g_strcmp0(bt, "FLOAT64") == 0));
    gboolean is_int = (bt && (g_strcmp0(bt, "INT8") == 0 || g_strcmp0(bt, "INT16") == 0 ||
                              g_strcmp0(bt, "INT32") == 0 || g_strcmp0(bt, "INT64") == 0 ||
                              g_strcmp0(bt, "INT8U") == 0 || g_strcmp0(bt, "INT16U") == 0 ||
                              g_strcmp0(bt, "INT32U") == 0 || g_strcmp0(bt, "INT64U") == 0 ||
                              g_strcmp0(bt, "CtlModels") == 0));

    if (is_bool) {
        input = gtk_switch_new();
        gtk_widget_set_halign(input, GTK_ALIGN_START);
        gtk_widget_set_valign(input, GTK_ALIGN_CENTER);
        if (da->value && g_strcmp0(da->value, "true") == 0)
            gtk_switch_set_active(GTK_SWITCH(input), TRUE);
    } else if (is_float) {
        input = gtk_spin_button_new_with_range(-1e12, 1e12, 0.01);
        if (da->value) gtk_spin_button_set_value(GTK_SPIN_BUTTON(input), atof(da->value));
    } else if (is_int) {
        input = gtk_spin_button_new_with_range(-2147483648.0, 2147483647.0, 1.0);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(input), 0);
        if (da->value) gtk_spin_button_set_value(GTK_SPIN_BUTTON(input), atof(da->value));
    } else {
        /* Fallback: text entry */
        input = gtk_entry_new();
        if (da->value) gtk_editable_set_text(GTK_EDITABLE(input), da->value);
    }
    gtk_box_append(GTK_BOX(box), input);

    /* Buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);

    GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
    g_signal_connect_swapped(btn_cancel, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
    gtk_box_append(GTK_BOX(btn_box), btn_cancel);

    WriteDialogCtx *ctx = g_new0(WriteDialogCtx, 1);
    ctx->app = app; ctx->bay_index = bay_index; ctx->da = da; ctx->input = input; ctx->dialog = dialog;
    ctx->is_bool = is_bool; ctx->is_float = is_float; ctx->is_int = is_int;

    GtkWidget *btn_write = gtk_button_new_with_label("Write");
    gtk_widget_add_css_class(btn_write, "suggested-action");
    g_signal_connect_data(btn_write, "clicked",
        G_CALLBACK(on_direct_write_confirmed), ctx,
        (GClosureNotify)write_dialog_ctx_free, 0);
    gtk_box_append(GTK_BOX(btn_box), btn_write);

    gtk_box_append(GTK_BOX(box), btn_box);
    gtk_window_present(GTK_WINDOW(dialog));
}

/* ================================================================== */
/*  Substation Configuration Dialog                                  */
/* ================================================================== */

typedef struct {
    AppState *app;
    GtkWidget *dialog;
    int current_bay;
    SubstationConfig temp_cfg;

    GtkWidget *sub_name_entry;
    GtkWidget *bay_dropdown;
    GtkWidget *test_status_lbl;

    GtkWidget *bay_label_entry;
    GtkWidget *host_entry;
    GtkWidget *port_entry;
    GtkWidget *ied_name_entry;
    GtkWidget *ld_inst_entry;
    GtkWidget *ln_cb_entry;
    GtkWidget *ln_ds_a_entry;
    GtkWidget *ln_ds_b_entry;
    GtkWidget *ln_line_ds_entry;
    GtkWidget *ln_earth_entry;
    GtkWidget *rcb_ref_entry;
    GtkWidget *equip_text_view;
} SubConfigDialogCtx;

static void equip_list_to_text(SldBayConfig *b, char *out, size_t outlen) {
    size_t pos = 0;
    for (int i = 0; i < b->num_equipment; i++) {
        SldEquipConfig *e = &b->equipment[i];
        char line[256];
        snprintf(line, sizeof(line), "%s | %s | %s.%s | type=%d\n",
            e->key[0] ? e->key : "EQ",
            e->label[0] ? e->label : e->key,
            e->ln[0] ? e->ln : "?",
            e->do_name[0] ? e->do_name : "Pos",
            e->equip_type);
        size_t L = strlen(line);
        if (pos + L + 1 >= outlen) break;
        memcpy(out + pos, line, L);
        pos += L;
    }
    out[pos] = 0;
}

/* Parse lines: KEY | Label | LN.DO | type=N
   LN examples: XCBR2, XSWI5, CSWI2, GGIO1. DO default Pos.
   type: 0=CB 1=DS_BUS 2=DS_LINE 3=EARTH */
static void text_to_equip_list(SldBayConfig *b, const char *txt) {
    char buf[2048];
    snprintf(buf, sizeof(buf), "%s", txt ? txt : "");
    /* Keep legacy fields as source for has_* flags; rebuild generic list. */
    int keep_legacy = (b->num_equipment == 0);
    if (!buf[0]) return;
    SldEquipConfig tmp[SLD_MAX_EQUIP_PER_BAY];
    int n = 0;
    char *line = strtok(buf, "\n");
    while (line && n < SLD_MAX_EQUIP_PER_BAY) {
        while (*line == ' ' || *line == '\t') line++;
        if (!*line || *line == '#') { line = strtok(NULL, "\n"); continue; }
        char key[32] = "", label[64] = "", lndo[64] = "";
        int etype = 1;
        char *p1 = strchr(line, '|');
        char *p2 = p1 ? strchr(p1 + 1, '|') : NULL;
        char *p3 = p2 ? strchr(p2 + 1, '|') : NULL;
        if (!p1 || !p2) { line = strtok(NULL, "\n"); continue; }
        *p1 = 0; *p2 = 0;
        if (p3) {
            *p3 = 0;
            char *pt = strstr(p3 + 1, "type=");
            if (pt) etype = atoi(pt + 5);
        } else {
            char *pt = strstr(p2 + 1, "type=");
            if (pt) {
                *pt = 0;
                etype = atoi(pt + 5);
            }
        }
        snprintf(key, sizeof(key), "%s", line);
        snprintf(label, sizeof(label), "%s", p1 + 1);
        snprintf(lndo, sizeof(lndo), "%s", p2 + 1);
        char *cln_p = strchr(lndo, '|');
        if (cln_p) *cln_p = 0;
        char *cln_t = strstr(lndo, "type=");
        if (cln_t) *cln_t = 0;
        /* trim */
        char *s;
        for (s = key; *s == ' ' || *s == '\t'; s++) {}
        memmove(key, s, strlen(s) + 1);
        for (s = label; *s == ' ' || *s == '\t'; s++) {}
        memmove(label, s, strlen(s) + 1);
        for (s = lndo; *s == ' ' || *s == '\t'; s++) {}
        memmove(lndo, s, strlen(s) + 1);
        char *end;
        end = key + strlen(key) - 1;
        while (end >= key && (*end == ' ' || *end == '\t')) *end-- = 0;
        end = label + strlen(label) - 1;
        while (end >= label && (*end == ' ' || *end == '\t')) *end-- = 0;
        end = lndo + strlen(lndo) - 1;
        while (end >= lndo && (*end == ' ' || *end == '\t' || *end == '\r')) *end-- = 0;
        /* split LN.DO */
        char ln[32] = "", doname[32] = "Pos";
        char *dot = strchr(lndo, '.');
        if (dot) {
            *dot = 0;
            snprintf(ln, sizeof(ln), "%s", lndo);
            snprintf(doname, sizeof(doname), "%s", dot + 1);
        } else {
            char *sp = strchr(lndo, ' ');
            if (sp) *sp = 0;
            snprintf(ln, sizeof(ln), "%s", lndo);
        }
        char *d_bar = strchr(doname, '|');
        if (d_bar) *d_bar = 0;
        char *d_sp = strchr(doname, ' ');
        if (d_sp) *d_sp = 0;
        char *d_end = doname + strlen(doname) - 1;
        while (d_end >= doname && (*d_end == ' ' || *d_end == '\t' || *d_end == '\r')) *d_end-- = 0;
        if (!doname[0]) strcpy(doname, "Pos");
        /* trim ln */
        while (ln[0] == ' ' || ln[0] == '\t') memmove(ln, ln + 1, strlen(ln));
        end = ln + strlen(ln) - 1;
        while (end >= ln && (*end == ' ' || *end == '\t')) *end-- = 0;
        if (!ln[0]) { line = strtok(NULL, "\n"); continue; }
        if (etype < 0 || etype > 3) etype = 1;
        SldEquipConfig *e = &tmp[n++];
        snprintf(e->key, sizeof(e->key), "%s", key[0] ? key : ln);
        snprintf(e->label, sizeof(e->label), "%s", label[0] ? label : key);
        snprintf(e->ln, sizeof(e->ln), "%s", ln);
        snprintf(e->do_name, sizeof(e->do_name), "%s", doname);
        snprintf(e->da_name, sizeof(e->da_name), "stVal");
        snprintf(e->fc_read, sizeof(e->fc_read), "ST");
        e->equip_type = etype;
        e->y_slot = 0.0;
        e->x_off = SLD_X_AUTO;
        line = strtok(NULL, "\n");
    }
    if (n > 0) {
        memcpy(b->equipment, tmp, sizeof(tmp[0]) * n);
        b->num_equipment = n;
    } else if (keep_legacy) {
        /* empty text keeps legacy list untouched */
    }
    (void)keep_legacy;
}

static void load_bay_into_form(SubConfigDialogCtx *ctx, int bay_idx) {
    if (bay_idx < 0 || bay_idx >= ctx->temp_cfg.num_bays) return;
    SldBayConfig *b = &ctx->temp_cfg.bays[bay_idx];
    
    gtk_editable_set_text(GTK_EDITABLE(ctx->bay_label_entry), b->name);
    gtk_editable_set_text(GTK_EDITABLE(ctx->host_entry), b->host);
    
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", b->port);
    gtk_editable_set_text(GTK_EDITABLE(ctx->port_entry), port_str);
    
    gtk_editable_set_text(GTK_EDITABLE(ctx->ied_name_entry), b->ied_name);
    gtk_editable_set_text(GTK_EDITABLE(ctx->ld_inst_entry), b->ld_inst);
    gtk_editable_set_text(GTK_EDITABLE(ctx->ln_cb_entry), b->ln_cb);
    gtk_editable_set_text(GTK_EDITABLE(ctx->ln_ds_a_entry), b->ln_ds_a);
    gtk_editable_set_text(GTK_EDITABLE(ctx->ln_ds_b_entry), b->ln_ds_b);
    gtk_editable_set_text(GTK_EDITABLE(ctx->ln_line_ds_entry), b->ln_line_ds);
    gtk_editable_set_text(GTK_EDITABLE(ctx->ln_earth_entry), b->ln_earth);
    gtk_editable_set_text(GTK_EDITABLE(ctx->rcb_ref_entry), b->rcb_ref);
    if (ctx->equip_text_view) {
        char ebuf[2048];
        equip_list_to_text(b, ebuf, sizeof(ebuf));
        GtkTextBuffer *tbuf = gtk_text_view_get_buffer(
            GTK_TEXT_VIEW(ctx->equip_text_view));
        gtk_text_buffer_set_text(tbuf, ebuf, -1);
    }

    gboolean is_conn = (bay_idx < SLD_MAX_BAYS && ctx->app->bays_con[bay_idx].connected);
    char sbuf[128];
    snprintf(sbuf, sizeof(sbuf), "Server: %s (%s:%d)",
             is_conn ? "CONNECTED (Online)" : "DISCONNECTED (Offline)",
             b->host, b->port);
    gtk_label_set_text(GTK_LABEL(ctx->test_status_lbl), sbuf);
    if (is_conn) {
        gtk_widget_add_css_class(ctx->test_status_lbl, "status-connected");
        gtk_widget_remove_css_class(ctx->test_status_lbl, "status-disconnected");
    } else {
        gtk_widget_add_css_class(ctx->test_status_lbl, "status-disconnected");
        gtk_widget_remove_css_class(ctx->test_status_lbl, "status-connected");
    }
}

static void sync_form_to_bay(SubConfigDialogCtx *ctx, int bay_idx) {
    if (bay_idx < 0 || bay_idx >= ctx->temp_cfg.num_bays) return;
    SldBayConfig *b = &ctx->temp_cfg.bays[bay_idx];

    strncpy(b->name, gtk_editable_get_text(GTK_EDITABLE(ctx->bay_label_entry)), sizeof(b->name) - 1);
    strncpy(b->host, gtk_editable_get_text(GTK_EDITABLE(ctx->host_entry)), sizeof(b->host) - 1);
    b->port = atoi(gtk_editable_get_text(GTK_EDITABLE(ctx->port_entry)));
    strncpy(b->ied_name, gtk_editable_get_text(GTK_EDITABLE(ctx->ied_name_entry)), sizeof(b->ied_name) - 1);
    strncpy(b->ld_inst, gtk_editable_get_text(GTK_EDITABLE(ctx->ld_inst_entry)), sizeof(b->ld_inst) - 1);
    strncpy(b->ln_cb, gtk_editable_get_text(GTK_EDITABLE(ctx->ln_cb_entry)), sizeof(b->ln_cb) - 1);
    strncpy(b->ln_ds_a, gtk_editable_get_text(GTK_EDITABLE(ctx->ln_ds_a_entry)), sizeof(b->ln_ds_a) - 1);
    strncpy(b->ln_ds_b, gtk_editable_get_text(GTK_EDITABLE(ctx->ln_ds_b_entry)), sizeof(b->ln_ds_b) - 1);
    strncpy(b->ln_line_ds, gtk_editable_get_text(GTK_EDITABLE(ctx->ln_line_ds_entry)), sizeof(b->ln_line_ds) - 1);
    strncpy(b->ln_earth, gtk_editable_get_text(GTK_EDITABLE(ctx->ln_earth_entry)), sizeof(b->ln_earth) - 1);
    strncpy(b->rcb_ref, gtk_editable_get_text(GTK_EDITABLE(ctx->rcb_ref_entry)), sizeof(b->rcb_ref) - 1);
    if (ctx->equip_text_view) {
        GtkTextBuffer *tbuf = gtk_text_view_get_buffer(
            GTK_TEXT_VIEW(ctx->equip_text_view));
        GtkTextIter s, e;
        gtk_text_buffer_get_bounds(tbuf, &s, &e);
        char *etxt = gtk_text_buffer_get_text(tbuf, &s, &e, FALSE);
        if (etxt && *etxt) text_to_equip_list(b, etxt);
        g_free(etxt);
    }

    /* Synchronize apparatus list from the explicit LN fields */
    bay_sync_equipment_from_legacy(b);

    b->has_line_ds = (b->ln_line_ds[0] != '\0') ? 1 : 0;
    b->has_earth = (b->ln_earth[0] != '\0') ? 1 : 0;
}

static void on_bay_dropdown_changed(GObject *gobject, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    SubConfigDialogCtx *ctx = (SubConfigDialogCtx *)user_data;
    GtkDropDown *dd = GTK_DROP_DOWN(gobject);
    guint selected = gtk_drop_down_get_selected(dd);

    if ((int)selected != ctx->current_bay) {
        sync_form_to_bay(ctx, ctx->current_bay);
        ctx->current_bay = (int)selected;
        load_bay_into_form(ctx, ctx->current_bay);
    }
}

static void on_test_link_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    SubConfigDialogCtx *ctx = (SubConfigDialogCtx *)user_data;
    const char *host = gtk_editable_get_text(GTK_EDITABLE(ctx->host_entry));
    int port = atoi(gtk_editable_get_text(GTK_EDITABLE(ctx->port_entry)));

    if (!host || !*host || port <= 0) {
        gtk_label_set_text(GTK_LABEL(ctx->test_status_lbl), "Invalid host/port");
        return;
    }

    gtk_label_set_text(GTK_LABEL(ctx->test_status_lbl), "Connecting...");

    IedConnection test_con = IedConnection_create();
    IedConnection_setConnectTimeout(test_con, 800);
    IedClientError err;
    IedConnection_connect(test_con, &err, host, port);

    if (err == IED_ERROR_OK) {
        gtk_label_set_text(GTK_LABEL(ctx->test_status_lbl), "✓ Link OK");
        gtk_widget_remove_css_class(ctx->test_status_lbl, "status-disconnected");
        gtk_widget_add_css_class(ctx->test_status_lbl, "status-connected");
        IedConnection_close(test_con);
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "✗ Failed (err %d)", err);
        gtk_label_set_text(GTK_LABEL(ctx->test_status_lbl), buf);
        gtk_widget_remove_css_class(ctx->test_status_lbl, "status-connected");
        gtk_widget_add_css_class(ctx->test_status_lbl, "status-disconnected");
    }
    IedConnection_destroy(test_con);
}

static void on_config_save_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    SubConfigDialogCtx *ctx = (SubConfigDialogCtx *)user_data;

    sync_form_to_bay(ctx, ctx->current_bay);

    strncpy(ctx->temp_cfg.substation_name,
            gtk_editable_get_text(GTK_EDITABLE(ctx->sub_name_entry)),
            sizeof(ctx->temp_cfg.substation_name) - 1);

    memcpy(&ctx->app->sub_config, &ctx->temp_cfg, sizeof(SubstationConfig));
    substation_config_save("config/substation_config.json", &ctx->app->sub_config);
    substation_config_save("../config/substation_config.json", &ctx->app->sub_config);
    app_reconnect_bays(ctx->app);

    set_status(ctx->app, "  Substation configuration saved and reconnected", "status-connected");
    gtk_window_destroy(GTK_WINDOW(ctx->dialog));
}

void show_substation_config_dialog(AppState *app, int initial_bay) {
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Substation Configuration & Bay Mapping");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 640, 560);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(app->window));

    SubConfigDialogCtx *ctx = g_new0(SubConfigDialogCtx, 1);
    ctx->app = app;
    ctx->dialog = dialog;
    memcpy(&ctx->temp_cfg, &app->sub_config, sizeof(SubstationConfig));
    ctx->current_bay = (initial_bay >= 0 && initial_bay < ctx->temp_cfg.num_bays) ? initial_bay : 0;

    g_object_set_data_full(G_OBJECT(dialog), "ctx", ctx, g_free);

    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(main_vbox, 16);
    gtk_widget_set_margin_end(main_vbox, 16);
    gtk_widget_set_margin_top(main_vbox, 16);
    gtk_widget_set_margin_bottom(main_vbox, 16);
    gtk_window_set_child(GTK_WINDOW(dialog), main_vbox);

    /* Top: Substation Name */
    GtkWidget *sub_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *lbl_sub = gtk_label_new("Substation Name:");
    ctx->sub_name_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(ctx->sub_name_entry), ctx->temp_cfg.substation_name);
    gtk_widget_set_hexpand(ctx->sub_name_entry, TRUE);
    gtk_box_append(GTK_BOX(sub_box), lbl_sub);
    gtk_box_append(GTK_BOX(sub_box), ctx->sub_name_entry);
    gtk_box_append(GTK_BOX(main_vbox), sub_box);

    /* Bay Selector Dropdown */
    GtkWidget *bay_sel_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *lbl_bay_sel = gtk_label_new("Select Bay:");
    gtk_box_append(GTK_BOX(bay_sel_box), lbl_bay_sel);

    const char *bay_items[SLD_MAX_BAYS + 1];
    char item_labels[SLD_MAX_BAYS][128];
    for (int i = 0; i < ctx->temp_cfg.num_bays; i++) {
        char clean_name[64];
        strncpy(clean_name, ctx->temp_cfg.bays[i].name, sizeof(clean_name) - 1);
        clean_name[sizeof(clean_name) - 1] = '\0';
        char *nl = strchr(clean_name, '\n');
        if (nl) *nl = ' ';
        snprintf(item_labels[i], sizeof(item_labels[i]), "Bay %d: %s (%s:%d)", 
                 i + 1, clean_name, ctx->temp_cfg.bays[i].host, ctx->temp_cfg.bays[i].port);
        bay_items[i] = item_labels[i];
    }
    bay_items[ctx->temp_cfg.num_bays] = NULL;

    GtkStringList *slist = gtk_string_list_new(bay_items);
    ctx->bay_dropdown = gtk_drop_down_new(G_LIST_MODEL(slist), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ctx->bay_dropdown), ctx->current_bay);
    gtk_widget_set_hexpand(ctx->bay_dropdown, TRUE);
    g_signal_connect(ctx->bay_dropdown, "notify::selected", G_CALLBACK(on_bay_dropdown_changed), ctx);
    gtk_box_append(GTK_BOX(bay_sel_box), ctx->bay_dropdown);
    gtk_box_append(GTK_BOX(main_vbox), bay_sel_box);

    /* Frame for Bay Settings */
    GtkWidget *frame = gtk_frame_new("Bay Parameters & Addressing");
    gtk_box_append(GTK_BOX(main_vbox), frame);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_margin_start(grid, 12);
    gtk_widget_set_margin_end(grid, 12);
    gtk_widget_set_margin_top(grid, 12);
    gtk_widget_set_margin_bottom(grid, 12);
    gtk_frame_set_child(GTK_FRAME(frame), grid);

    int row = 0;

    /* Bay Display Label */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Bay Label:"), 0, row, 1, 1);
    ctx->bay_label_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), ctx->bay_label_entry, 1, row, 2, 1);
    row++;

    /* Host and Port */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Server Host (IP):"), 0, row, 1, 1);
    ctx->host_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), ctx->host_entry, 1, row, 1, 1);

    GtkWidget *test_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *btn_test = gtk_button_new_with_label("Test Link");
    g_signal_connect(btn_test, "clicked", G_CALLBACK(on_test_link_clicked), ctx);
    ctx->test_status_lbl = gtk_label_new("");
    gtk_box_append(GTK_BOX(test_box), btn_test);
    gtk_box_append(GTK_BOX(test_box), ctx->test_status_lbl);
    gtk_grid_attach(GTK_GRID(grid), test_box, 2, row, 1, 1);
    row++;

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("MMS Port:"), 0, row, 1, 1);
    ctx->port_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), ctx->port_entry, 1, row, 2, 1);
    row++;

    /* Logical Addressing Separator */
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_grid_attach(GTK_GRID(grid), sep, 0, row, 3, 1);
    row++;

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("IED Name:"), 0, row, 1, 1);
    ctx->ied_name_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), ctx->ied_name_entry, 1, row, 2, 1);
    row++;

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("LDevice Inst:"), 0, row, 1, 1);
    ctx->ld_inst_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), ctx->ld_inst_entry, 1, row, 2, 1);
    row++;

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Circuit Breaker (LN):"), 0, row, 1, 1);
    ctx->ln_cb_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), ctx->ln_cb_entry, 1, row, 2, 1);
    row++;

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Bus DS-A (LN):"), 0, row, 1, 1);
    ctx->ln_ds_a_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), ctx->ln_ds_a_entry, 1, row, 2, 1);
    row++;

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Bus DS-B (LN):"), 0, row, 1, 1);
    ctx->ln_ds_b_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), ctx->ln_ds_b_entry, 1, row, 2, 1);
    row++;

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Line DS (LN):"), 0, row, 1, 1);
    ctx->ln_line_ds_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), ctx->ln_line_ds_entry, 1, row, 2, 1);
    row++;

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Earth Switch (LN):"), 0, row, 1, 1);
    ctx->ln_earth_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), ctx->ln_earth_entry, 1, row, 2, 1);
    row++;

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("URCB Report Ref:"), 0, row, 1, 1);
    ctx->rcb_ref_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), ctx->rcb_ref_entry, 1, row, 2, 1);
    row++;

    gtk_grid_attach(GTK_GRID(grid),
        gtk_label_new("Custom LN list (KEY | Label | LN.DO | type=0CB/1DS/2Line/3ES):"),
        0, row, 3, 1);
    row++;
    ctx->equip_text_view = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(ctx->equip_text_view), TRUE);
    gtk_widget_set_size_request(ctx->equip_text_view, -1, 110);
    GtkWidget *equip_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(equip_scroll), ctx->equip_text_view);
    gtk_widget_set_vexpand(equip_scroll, FALSE);
    gtk_grid_attach(GTK_GRID(grid), equip_scroll, 0, row, 3, 1);
    row++;

    load_bay_into_form(ctx, ctx->current_bay);

    /* Bottom Action Buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);
    gtk_widget_set_margin_top(btn_box, 8);

    GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
    g_signal_connect_swapped(btn_cancel, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
    gtk_box_append(GTK_BOX(btn_box), btn_cancel);

    GtkWidget *btn_save = gtk_button_new_with_label("Save & Apply");
    gtk_widget_add_css_class(btn_save, "suggested-action");
    g_signal_connect(btn_save, "clicked", G_CALLBACK(on_config_save_clicked), ctx);
    gtk_box_append(GTK_BOX(btn_box), btn_save);

    gtk_box_append(GTK_BOX(main_vbox), btn_box);
    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_substation_config_clicked(gpointer user_data) {
    AppState *app = (AppState *)user_data;
    show_substation_config_dialog(app, 0);
}

static void on_cfg_bay_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    CtlDialogCtx *c = (CtlDialogCtx *)user_data;
    AppState *app = c->app;
    int bidx = c->bay_index;
    gtk_window_destroy(GTK_WINDOW(c->dialog));
    show_substation_config_dialog(app, bidx >= 0 ? bidx : 0);
}

/* ----- Control Actions Dialog (CO) ----- */

/* Recursively find an IedModelNode by its full ACSI reference path. */
static IedModelNode *
find_node_by_ref(IedModelNode *parent, const char *ref)
{
    if (!parent || !ref) return NULL;
    if (g_strcmp0(parent->ref, ref) == 0) return parent;
    for (guint i = 0; i < parent->children->len; i++) {
        IedModelNode *found = find_node_by_ref(
            g_ptr_array_index(parent->children, i), ref);
        if (found) return found;
    }
    return NULL;
}

void
show_control_dialog(AppState *app, int bay_index, IedModelNode *da)
{
    /* Find the parent DO reference for ControlObjectClient.
       Walk up from the DA until we find a DO-level node. */
    IedModelNode *do_node = da;
    /* If the selected node is a child of Oper/SBO/Cancel, walk up */
    /* For CO DAs the DO ref is typically 2 levels up (DO -> Oper -> ctlVal) */
    /* We'll use a heuristic: strip after the last known CO sub-structure */
    char doRef[512];
    g_strlcpy(doRef, da->ref, sizeof(doRef));
    /* Strip Oper/SBO/SBOw/Cancel and anything below from the ref */
    char *oper_pos = strstr(doRef, ".Oper");
    if (oper_pos) *oper_pos = '\0';
    char *sbo_pos = strstr(doRef, ".SBO");
    if (sbo_pos) *sbo_pos = '\0';
    char *cancel_pos = strstr(doRef, ".Cancel");
    if (cancel_pos) *cancel_pos = '\0';
    /* Also strip leaf DAs like .ctlVal, .stVal if at DO level */
    char *ctlval_pos = strstr(doRef, ".ctlVal");
    if (ctlval_pos) *ctlval_pos = '\0';
    /* Strip accidental pipe/type formatting if present */
    char *pipe_pos = strchr(doRef, '|');
    if (pipe_pos) *pipe_pos = '\0';
    char *end_pos = doRef + strlen(doRef) - 1;
    while (end_pos >= doRef && (*end_pos == ' ' || *end_pos == '\t' || *end_pos == '\r')) *end_pos-- = '\0';

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Control Actions");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 260);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(app->window));

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_window_set_child(GTK_WINDOW(dialog), box);

    char ref_txt[512];
    snprintf(ref_txt, sizeof(ref_txt), "DO: %s  [Control]", doRef);
    GtkWidget *lbl_ref = gtk_label_new(ref_txt);
    gtk_label_set_xalign(GTK_LABEL(lbl_ref), 0);
    gtk_widget_add_css_class(lbl_ref, "breadcrumb");
    gtk_box_append(GTK_BOX(box), lbl_ref);

    /* ctlVal input — default to boolean switch for SPC/DPC */
    GtkWidget *lbl_ctl = gtk_label_new("Control Value (Boolean):");
    gtk_label_set_xalign(GTK_LABEL(lbl_ctl), 0);
    gtk_box_append(GTK_BOX(box), lbl_ctl);

    GtkWidget *ctl_switch = gtk_switch_new();
    gtk_widget_set_halign(ctl_switch, GTK_ALIGN_START);
    gtk_widget_set_valign(ctl_switch, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), ctl_switch);

    /* Resolve ctlModel from the discovered model tree for this connection.
     * Prefer the bay-specific model_root when operating an SLD element. */
    IedModelNode *model_root = NULL;
    if (bay_index >= 0 && bay_index < SLD_NUM_BAYS)
        model_root = app->bays_con[bay_index].model_root;
    if (!model_root)
        model_root = app->ui_model_root;

    const char *model_str = "(automatic detection)";
    if (model_root) {
        IedModelNode *do_node = find_node_by_ref(model_root, doRef);
        if (do_node) {
            for (guint ci = 0; ci < do_node->children->len; ci++) {
                IedModelNode *ch = g_ptr_array_index(do_node->children, ci);
                if (g_strcmp0(ch->name, "ctlModel") == 0 && ch->value) {
                    model_str = ch->value;
                    break;
                }
            }
        }
    }

    char model_lbl_txt[256];
    snprintf(model_lbl_txt, sizeof(model_lbl_txt), "ctlModel: %s", model_str);
    GtkWidget *lbl_model = gtk_label_new(model_lbl_txt);
    gtk_label_set_xalign(GTK_LABEL(lbl_model), 0);
    gtk_box_append(GTK_BOX(box), lbl_model);

    /* Buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);

    CtlDialogCtx *ctx = g_new0(CtlDialogCtx, 1);
    ctx->app = app; ctx->bay_index = bay_index; g_strlcpy(ctx->doRef, doRef, sizeof(ctx->doRef));
    ctx->ctl_switch = ctl_switch; ctx->dialog = dialog;

    GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
    g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_control_cancel_confirmed), ctx);
    gtk_box_append(GTK_BOX(btn_box), btn_cancel);
    ctx->app = app; ctx->bay_index = bay_index; g_strlcpy(ctx->doRef, doRef, sizeof(ctx->doRef));
    ctx->ctl_switch = ctl_switch; ctx->dialog = dialog;

    /* Bind context lifecycle to the dialog window */
    g_object_set_data_full(G_OBJECT(dialog), "ctx", ctx, ctl_dialog_ctx_free);

    GtkWidget *btn_select = gtk_button_new_with_label("Select SBO");
    gtk_widget_add_css_class(btn_select, "suggested-action");
    g_signal_connect(btn_select, "clicked", G_CALLBACK(on_control_select_confirmed), ctx);
    gtk_box_append(GTK_BOX(btn_box), btn_select);

    GtkWidget *btn_operate = gtk_button_new_with_label("Operate");
    gtk_widget_add_css_class(btn_operate, "destructive-action");
    g_signal_connect(btn_operate, "clicked", G_CALLBACK(on_control_operate_confirmed), ctx);
    gtk_box_append(GTK_BOX(btn_box), btn_operate);

    GtkWidget *btn_sel_op = gtk_button_new_with_label("Select & Operate");
    gtk_widget_add_css_class(btn_sel_op, "destructive-action");
    g_signal_connect(btn_sel_op, "clicked", G_CALLBACK(on_control_sel_op_confirmed), ctx);
    gtk_box_append(GTK_BOX(btn_box), btn_sel_op);

    GtkWidget *btn_cfg_bay = gtk_button_new_with_label("⚙ Configure Bay");
    gtk_widget_add_css_class(btn_cfg_bay, "action-btn");
    g_signal_connect(btn_cfg_bay, "clicked", G_CALLBACK(on_cfg_bay_clicked), ctx);
    gtk_box_append(GTK_BOX(btn_box), btn_cfg_bay);

    gtk_box_append(GTK_BOX(box), btn_box);
    gtk_window_present(GTK_WINDOW(dialog));
}

/* ----- Write button click handler ----- */

static int get_bay_index_from_ref(AppState *app, const char *ref) {
    if (!ref || !app) return -1;
    for (int i = 0; i < app->sub_config.num_bays; i++) {
        const char *ied_name = app->sub_config.bays[i].ied_name;
        const char *ld_inst = app->sub_config.bays[i].ld_inst;
        if (ied_name[0] && strncmp(ref, ied_name, strlen(ied_name)) == 0) return i;
        if (ld_inst[0] && strstr(ref, ld_inst)) return i;
    }
    return -1;
}

static void on_write_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AppState *app = (AppState *)user_data;
    IedModelNode *da = app->selected_da;
    if (!da || !da->fc) return;

    /* Since this is from the IED explorer (Data View), we write to the explorer connection (bay_index = -1) */
    if (g_strcmp0(da->fc, "CO") == 0) {
        show_control_dialog(app, -1, da);
    } else {
        show_direct_write_dialog(app, -1, da);
    }
}

/* ================================================================== */
/*  Column factories — LEFT PANE TREE                                 */
/* ================================================================== */

static void tree_setup(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    (void)f; (void)d;
    GtkWidget *expander = gtk_tree_expander_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *icon = gtk_image_new();
    GtkWidget *lbl = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0);
    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), lbl);
    gtk_tree_expander_set_child(GTK_TREE_EXPANDER(expander), box);
    gtk_list_item_set_child(li, expander);
}

static void tree_bind(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    (void)f; (void)d;
    GtkTreeListRow *row = GTK_TREE_LIST_ROW(gtk_list_item_get_item(li));
    GtkWidget *expander = gtk_list_item_get_child(li);
    gtk_tree_expander_set_list_row(GTK_TREE_EXPANDER(expander), row);

    IecTreeItem *ti = IEC_TREE_ITEM(gtk_tree_list_row_get_item(row));
    GtkWidget *box = gtk_tree_expander_get_child(GTK_TREE_EXPANDER(expander));
    GtkWidget *icon = gtk_widget_get_first_child(box);
    GtkWidget *lbl = gtk_widget_get_next_sibling(icon);

    gtk_image_set_from_icon_name(GTK_IMAGE(icon), ti->icon);
    gtk_label_set_text(GTK_LABEL(lbl), ti->name);

    /* Color by type */
    gtk_widget_remove_css_class(lbl, "tree-ld");
    gtk_widget_remove_css_class(lbl, "tree-ln");
    gtk_widget_add_css_class(lbl, ti->node_type == NODE_LD ? "tree-ld" : "tree-ln");

    g_object_unref(ti);
}

/* ================================================================== */
/*  Column factories — RIGHT PANE DATA TABLE                          */
/* ================================================================== */

/* --- Name column with badge + expander --- */
static void name_setup(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    (void)f; (void)d;
    GtkWidget *expander = gtk_tree_expander_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *badge_type = gtk_label_new("");
    gtk_widget_add_css_class(badge_type, "badge");
    GtkWidget *badge_fc = gtk_label_new("");
    gtk_widget_add_css_class(badge_fc, "badge");
    GtkWidget *name_lbl = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(name_lbl), 0);
    gtk_box_append(GTK_BOX(box), badge_type);
    gtk_box_append(GTK_BOX(box), badge_fc);
    gtk_box_append(GTK_BOX(box), name_lbl);
    gtk_tree_expander_set_child(GTK_TREE_EXPANDER(expander), box);
    gtk_list_item_set_child(li, expander);
}

static void name_bind(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    (void)f; (void)d;
    GtkTreeListRow *row = GTK_TREE_LIST_ROW(gtk_list_item_get_item(li));
    GtkWidget *expander = gtk_list_item_get_child(li);
    gtk_tree_expander_set_list_row(GTK_TREE_EXPANDER(expander), row);

    IecRowItem *ri = IEC_ROW_ITEM(gtk_tree_list_row_get_item(row));
    GtkWidget *box = gtk_tree_expander_get_child(GTK_TREE_EXPANDER(expander));
    GtkWidget *badge_type = gtk_widget_get_first_child(box);
    GtkWidget *badge_fc = gtk_widget_get_next_sibling(badge_type);
    GtkWidget *name_lbl = gtk_widget_get_next_sibling(badge_fc);

    /* DO/DA badge */
    gtk_label_set_text(GTK_LABEL(badge_type), ri->badge_type);
    gtk_widget_remove_css_class(badge_type, "badge-do");
    gtk_widget_remove_css_class(badge_type, "badge-da");
    gtk_widget_add_css_class(badge_type, node_type_badge_css(ri->node_type));

    /* FC badge */
    if (ri->fc && ri->fc[0]) {
        gtk_label_set_text(GTK_LABEL(badge_fc), ri->fc);
        gtk_widget_set_visible(badge_fc, TRUE);
        gtk_widget_remove_css_class(badge_fc, "badge-mx");
        gtk_widget_remove_css_class(badge_fc, "badge-st");
        gtk_widget_remove_css_class(badge_fc, "badge-cf");
        gtk_widget_remove_css_class(badge_fc, "badge-co");
        gtk_widget_remove_css_class(badge_fc, "badge-dc");
        gtk_widget_remove_css_class(badge_fc, "badge-fc");
        gtk_widget_add_css_class(badge_fc, fc_badge_css(ri->fc));
    } else {
        gtk_widget_set_visible(badge_fc, FALSE);
    }

    gtk_label_set_text(GTK_LABEL(name_lbl), ri->name);
    g_object_unref(ri);
}

/* --- Simple text column factory helper --- */
typedef const char *(*RowFieldGetter)(IecRowItem *);

typedef struct { RowFieldGetter getter; const char *css; } ColBindData;

static gboolean update_label_tick_cb(GtkWidget *widget, GdkFrameClock *frame_clock, gpointer user_data) {
    (void)frame_clock;
    ColBindData *cbd = (ColBindData *)user_data;
    IecRowItem *ri = g_object_get_data(G_OBJECT(widget), "row-item");
    if (ri) {
        const char *text = cbd->getter(ri);
        if (text && g_strcmp0(gtk_label_get_text(GTK_LABEL(widget)), text) != 0) {
            gtk_label_set_text(GTK_LABEL(widget), text);
        }
    }
    return G_SOURCE_CONTINUE;
}

static void text_col_setup(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    (void)f;
    ColBindData *cbd = (ColBindData *)d;
    GtkWidget *lbl = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0);
    if (cbd->css) gtk_widget_add_css_class(lbl, cbd->css);
    gtk_list_item_set_child(li, lbl);
    gtk_widget_add_tick_callback(lbl, (GtkTickCallback)update_label_tick_cb, cbd, NULL);
}

static void text_col_bind(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    (void)f;
    ColBindData *cbd = (ColBindData *)d;
    GtkTreeListRow *row = GTK_TREE_LIST_ROW(gtk_list_item_get_item(li));
    IecRowItem *ri = IEC_ROW_ITEM(gtk_tree_list_row_get_item(row));
    GtkWidget *lbl = gtk_list_item_get_child(li);
    
    g_object_set_data_full(G_OBJECT(lbl), "row-item", g_object_ref(ri), g_object_unref);

    const char *text = cbd->getter(ri);
    gtk_label_set_text(GTK_LABEL(lbl), text ? text : "");
    g_object_unref(ri);
}

static void text_col_unbind(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    (void)f; (void)d;
    GtkWidget *lbl = gtk_list_item_get_child(li);
    g_object_set_data(G_OBJECT(lbl), "row-item", NULL);
}

static const char *get_value(IecRowItem *ri) {
    return (ri->model_node && ri->model_node->value) ? ri->model_node->value : "";
}
static const char *get_type_str(IecRowItem *ri) {
    /* Type column: for DOs show "DO", for DAs show IEC 61850 BasicType */
    if (ri->model_node) {
        NodeType t = ri->model_node->type;
        if (t == NODE_DO) return "DO";
        if (t == NODE_DA) {
            /* Prefer IEC 61850 BasicType (§02/§03), fallback to MmsType */
            if (ri->model_node->basic_type && ri->model_node->basic_type[0])
                return ri->model_node->basic_type;
            return ri->model_node->mms_type ? ri->model_node->mms_type : "";
        }
    }
    return "";
}
static const char *get_mms_type(IecRowItem *ri) {
    return (ri->model_node && ri->model_node->mms_type) ? ri->model_node->mms_type : "";
}
static const char *get_timestamp(IecRowItem *ri) {
    return (ri->model_node && ri->model_node->timestamp) ? ri->model_node->timestamp : "";
}
static const char *get_description(IecRowItem *ri) {
    return (ri->model_node && ri->model_node->description) ? ri->model_node->description : "";
}

/* Static bind data (lives for process lifetime) */
static ColBindData cbd_value  = { get_value,       "col-value" };
static ColBindData cbd_type   = { get_type_str,    "col-type"  };
static ColBindData cbd_mms    = { get_mms_type,    "col-mms"   };
static ColBindData cbd_time   = { get_timestamp,   "col-time"  };
static ColBindData cbd_desc   = { get_description, "col-desc"  };

static GtkColumnViewColumn *
make_text_column(const char *title, ColBindData *cbd, int fixed_w, gboolean expand)
{
    GtkListItemFactory *fac = gtk_signal_list_item_factory_new();
    g_signal_connect(fac, "setup", G_CALLBACK(text_col_setup), cbd);
    g_signal_connect(fac, "bind",  G_CALLBACK(text_col_bind),  cbd);
    g_signal_connect(fac, "unbind", G_CALLBACK(text_col_unbind), NULL);
    GtkColumnViewColumn *col = gtk_column_view_column_new(title, fac);
    if (fixed_w > 0) gtk_column_view_column_set_fixed_width(col, fixed_w);
    gtk_column_view_column_set_expand(col, expand);
    return col;
}

/* ================================================================== */
/*  CSS provider                                                      */
/* ================================================================== */

static void load_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *paths[] = { "res/style.css", "../res/style.css", NULL };
    const char *chosen = NULL;
    for (int i = 0; paths[i]; i++) {
        if (g_file_test(paths[i], G_FILE_TEST_EXISTS)) { chosen = paths[i]; break; }
    }
    if (chosen) {
        gtk_css_provider_load_from_path(provider, chosen);
    } else {
        g_printerr("Warning: style.css not found\n");
        g_object_unref(provider); return;
    }
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

/* ================================================================== */
/*  SLD callbacks                                                     */
/* ================================================================== */

static void sld_draw_cb(GtkDrawingArea *area, cairo_t *cr,
                        int width, int height, gpointer user_data)
{
    (void)area;
    AppState *app = (AppState *)user_data;
    int bay_connected[SLD_MAX_BAYS] = {0};
    for (int i = 0; i < app->sub_config.num_bays && i < SLD_MAX_BAYS; i++) {
        bay_connected[i] = app->bays_con[i].connected ? 1 : 0;
    }
    sld_draw(cr, width, height,
             app->sld_equipment, app->sld_equipment_count,
             &app->sub_config, bay_connected);
}

static void sld_click_cb(GtkGestureClick *gesture, int n_press,
                         double x, double y, gpointer user_data)
{
    (void)n_press;
    AppState *app = (AppState *)user_data;
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    int w = gtk_widget_get_width(widget);
    int h = gtk_widget_get_height(widget);
    int idx = sld_hit_test(app->sld_equipment, app->sld_equipment_count, x, y, w, h);
    if (idx < 0) return;

    SldEquipment *e = &app->sld_equipment[idx];
    printf("[SLD] Clicked: %s (%s) — state=%d\n", e->label, e->doRef, e->state);

    if (!app->bays_con[e->bay_index].connected) {
        printf("[SLD] Disconnected — showing control dialog in preview mode\n");
    }

    /* Build a minimal DA node for the control dialog */
    IedModelNode dummy;
    memset(&dummy, 0, sizeof(dummy));
    dummy.ref = e->doRef;
    dummy.fc = "CO";
    dummy.name = e->label;
    dummy.basic_type = "BOOLEAN";
    dummy.value = (e->state == DBPOS_CLOSED) ? "true" : "false";
    show_control_dialog(app, e->bay_index, &dummy);
}

/* ================================================================== */
/*  Build the window                                                  */
/* ================================================================== */

void app_gui_init(AppState *app, GtkApplication *gtk_app) {
    app->gtk_app = gtk_app;
    load_css();

    /* ---- Window ---- */
    app->window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(app->window), "IEC 61850 Client");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1280, 760);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(app->window), vbox);

    /* ================================================================ */
    /*  TOOLBAR                                                         */
    /* ================================================================ */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(toolbar, "toolbar");
    gtk_widget_set_margin_start(toolbar, 10);
    gtk_widget_set_margin_end(toolbar, 10);
    gtk_widget_set_margin_top(toolbar, 6);
    gtk_widget_set_margin_bottom(toolbar, 6);

    GtkWidget *lbl_host = gtk_label_new("Host:");
    gtk_box_append(GTK_BOX(toolbar), lbl_host);

    app->host_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(app->host_entry), "127.0.0.1");
    gtk_widget_set_size_request(app->host_entry, 160, -1);
    gtk_box_append(GTK_BOX(toolbar), app->host_entry);

    GtkWidget *lbl_port = gtk_label_new("  Port:");
    gtk_box_append(GTK_BOX(toolbar), lbl_port);

    app->port_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(app->port_entry), "102");
    gtk_widget_set_size_request(app->port_entry, 60, -1);
    gtk_box_append(GTK_BOX(toolbar), app->port_entry);

    app->connect_btn = gtk_button_new_with_label("  Connect");
    gtk_widget_set_margin_start(app->connect_btn, 8);
    g_signal_connect(app->connect_btn, "clicked", G_CALLBACK(on_connect_clicked), app);
    gtk_box_append(GTK_BOX(toolbar), app->connect_btn);

    /* Spacer */
    GtkWidget *spacer = gtk_label_new("");
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(toolbar), spacer);

    /* Read All button */
    GtkWidget *btn_read = gtk_button_new_with_label("Read All");
    gtk_widget_add_css_class(btn_read, "action-btn");
    g_signal_connect(btn_read, "clicked", G_CALLBACK(on_read_all_clicked), app);
    gtk_box_append(GTK_BOX(toolbar), btn_read);

    /* Auto-Poll toggle button */
    GtkWidget *btn_poll = gtk_toggle_button_new_with_label("Auto-Poll");
    gtk_widget_add_css_class(btn_poll, "action-btn");
    g_signal_connect(btn_poll, "toggled", G_CALLBACK(on_auto_poll_toggled), app);
    gtk_box_append(GTK_BOX(toolbar), btn_poll);
    app->auto_poll_btn = btn_poll;

    /* Write button (starts disabled) */
    app->write_btn = gtk_button_new_with_label("Write");
    gtk_widget_add_css_class(app->write_btn, "action-btn");
    gtk_widget_add_css_class(app->write_btn, "destructive-action");
    gtk_widget_set_sensitive(app->write_btn, FALSE);
    gtk_widget_set_tooltip_text(app->write_btn, "Select a Data Attribute");
    g_signal_connect(app->write_btn, "clicked", G_CALLBACK(on_write_clicked), app);
    gtk_box_append(GTK_BOX(toolbar), app->write_btn);

    /* Report button (placeholder) */
    GtkWidget *btn_report = gtk_button_new_with_label("Report");
    gtk_widget_add_css_class(btn_report, "action-btn");
    gtk_box_append(GTK_BOX(toolbar), btn_report);

    /* Substation Config button */
    GtkWidget *btn_cfg = gtk_button_new_with_label("  Substation Config");
    gtk_button_set_icon_name(GTK_BUTTON(btn_cfg), "emblem-system-symbolic");
    gtk_widget_add_css_class(btn_cfg, "action-btn");
    g_signal_connect_swapped(btn_cfg, "clicked", G_CALLBACK(on_substation_config_clicked), app);
    gtk_box_append(GTK_BOX(toolbar), btn_cfg);

    gtk_box_append(GTK_BOX(vbox), toolbar);
    gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* ================================================================ */
    /*  MAIN CONTENT — GtkPaned                                         */
    /* ================================================================ */
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(paned), 240);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_box_append(GTK_BOX(vbox), paned);

    /* ---- LEFT PANE: IED Explorer Tree ---- */
    GtkWidget *left_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(left_box, "explorer-pane");

    GtkWidget *left_title = gtk_label_new("  IED Explorer");
    gtk_label_set_xalign(GTK_LABEL(left_title), 0);
    gtk_widget_add_css_class(left_title, "pane-title");
    gtk_box_append(GTK_BOX(left_box), left_title);

    app->tree_store = g_list_store_new(IEC_TYPE_TREE_ITEM);
    GtkTreeListModel *tree_model = gtk_tree_list_model_new(
        G_LIST_MODEL(app->tree_store), FALSE, TRUE, tree_children_func, NULL, NULL);
    GtkSingleSelection *tree_sel = gtk_single_selection_new(G_LIST_MODEL(tree_model));
    g_signal_connect(tree_sel, "selection-changed", G_CALLBACK(on_tree_selection_changed), app);

    GtkListItemFactory *tree_fac = gtk_signal_list_item_factory_new();
    g_signal_connect(tree_fac, "setup", G_CALLBACK(tree_setup), NULL);
    g_signal_connect(tree_fac, "bind",  G_CALLBACK(tree_bind),  NULL);

    app->tree_view = gtk_list_view_new(GTK_SELECTION_MODEL(tree_sel), tree_fac);
    gtk_widget_add_css_class(app->tree_view, "explorer-tree");

    GtkWidget *tree_sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(tree_sw), app->tree_view);
    gtk_widget_set_vexpand(tree_sw, TRUE);
    gtk_box_append(GTK_BOX(left_box), tree_sw);

    gtk_paned_set_start_child(GTK_PANED(paned), left_box);

    /* ---- RIGHT PANE: Notebook with Data View + SLD View ---- */
    GtkWidget *notebook = gtk_notebook_new();
    gtk_widget_add_css_class(notebook, "data-pane");

    /* === Tab 1: Data View === */
    GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Breadcrumb path */
    app->path_label = gtk_label_new("  Select a Logical Node…");
    gtk_label_set_xalign(GTK_LABEL(app->path_label), 0);
    gtk_widget_add_css_class(app->path_label, "breadcrumb");
    gtk_box_append(GTK_BOX(right_box), app->path_label);

    /* Data table with GtkTreeListModel for expandable rows */
    app->data_store = g_list_store_new(IEC_TYPE_ROW_ITEM);
    GtkTreeListModel *data_tree_model = gtk_tree_list_model_new(
        G_LIST_MODEL(app->data_store), FALSE, FALSE, data_children_func, NULL, NULL);
    GtkSingleSelection *data_sel = gtk_single_selection_new(G_LIST_MODEL(data_tree_model));
    gtk_single_selection_set_autoselect(data_sel, FALSE);
    gtk_single_selection_set_can_unselect(data_sel, TRUE);
    g_signal_connect(data_sel, "selection-changed", G_CALLBACK(on_data_selection_changed), app);

    app->col_view = gtk_column_view_new(GTK_SELECTION_MODEL(data_sel));
    gtk_widget_set_vexpand(app->col_view, TRUE);
    gtk_widget_add_css_class(app->col_view, "data-table");

    /* Name column (with badge + expander) */
    GtkListItemFactory *fac_name = gtk_signal_list_item_factory_new();
    g_signal_connect(fac_name, "setup", G_CALLBACK(name_setup), NULL);
    g_signal_connect(fac_name, "bind",  G_CALLBACK(name_bind),  NULL);
    GtkColumnViewColumn *col_name = gtk_column_view_column_new("Name", fac_name);
    gtk_column_view_column_set_expand(col_name, TRUE);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(app->col_view), col_name);

    /* Value */
    gtk_column_view_append_column(GTK_COLUMN_VIEW(app->col_view),
        make_text_column("Value", &cbd_value, 140, FALSE));
    /* Type */
    gtk_column_view_append_column(GTK_COLUMN_VIEW(app->col_view),
        make_text_column("Type", &cbd_type, 200, FALSE));
    /* MmsType */
    gtk_column_view_append_column(GTK_COLUMN_VIEW(app->col_view),
        make_text_column("MmsType", &cbd_mms, 120, FALSE));
    /* UpdateDateTime */
    gtk_column_view_append_column(GTK_COLUMN_VIEW(app->col_view),
        make_text_column("UpdateDateTime", &cbd_time, 180, FALSE));
    /* Description */
    gtk_column_view_append_column(GTK_COLUMN_VIEW(app->col_view),
        make_text_column("Description", &cbd_desc, 0, TRUE));

    GtkWidget *data_sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(data_sw), app->col_view);
    gtk_widget_set_vexpand(data_sw, TRUE);
    gtk_box_append(GTK_BOX(right_box), data_sw);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), right_box,
                             gtk_label_new("Data View"));

    /* === Tab 2: SLD View === */
    sld_init_equipment(app->sld_equipment, &app->sld_equipment_count, &app->sub_config);

    app->sld_area = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(app->sld_area), 1300);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(app->sld_area), 590);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app->sld_area),
                                   sld_draw_cb, app, NULL);

    /* Click gesture for SLD interaction */
    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(sld_click_cb), app);
    gtk_widget_add_controller(app->sld_area, GTK_EVENT_CONTROLLER(click));

    GtkWidget *sld_sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sld_sw), app->sld_area);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), sld_sw,
                             gtk_label_new("SLD View"));

    gtk_paned_set_end_child(GTK_PANED(paned), notebook);

    /* ================================================================ */
    /*  STATUS BAR                                                      */
    /* ================================================================ */
    app->status_label = gtk_label_new("  Disconnected");
    gtk_label_set_xalign(GTK_LABEL(app->status_label), 0);
    gtk_widget_add_css_class(app->status_label, "status-bar");
    gtk_widget_add_css_class(app->status_label, "status-disconnected");
    gtk_box_append(GTK_BOX(vbox), app->status_label);

    gtk_window_present(GTK_WINDOW(app->window));
}
