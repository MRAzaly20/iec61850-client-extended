/* ============================================================
 *  iec61850_app.h — Core application types for the IEC 61850 Client
 * ============================================================ */
#ifndef IEC61850_APP_H
#define IEC61850_APP_H

#include <gtk/gtk.h>
#include "iec61850_client.h"
#include "sld_data.h"
#include "substation_config.h"

/* ---- Data model tree node ---- */

typedef enum {
    NODE_ROOT,
    NODE_LD,
    NODE_LN,
    NODE_DO,
    NODE_DA
} NodeType;

typedef struct _IedModelNode IedModelNode;
struct _IedModelNode {
    char       *name;       /* short name  (e.g. "GGIO1")              */
    char       *ref;        /* full reference (e.g. "simpleIO/GGIO1")  */
    NodeType    type;
    char       *fc;         /* functional constraint (MX, ST, CF, …)   */
    char       *value;      /* last known value string                 */
    char       *mms_type;   /* MMS type string                         */
    char       *basic_type; /* IEC 61850 BasicType / semantic type     */
    char       *description;/* CDC / description text                  */
    char       *timestamp;  /* last update time string                 */
    GPtrArray  *children;   /* GPtrArray of IedModelNode*              */
};

/* ---- Report callback payload (heap-allocated, freed in UI idle) ---- */

#define REPORT_MAX_ENTRIES 64
#define REPORT_ENTRY_LEN   512

typedef struct {
    char  rcb_ref[256];
    char  entries[REPORT_MAX_ENTRIES][REPORT_ENTRY_LEN];
    int   count;
} ReportCallbackData;

/* ---- GObject wrapper for tree explorer items (left pane) ---- */

#define IEC_TYPE_TREE_ITEM (iec_tree_item_get_type())
G_DECLARE_FINAL_TYPE(IecTreeItem, iec_tree_item, IEC, TREE_ITEM, GObject)

struct _IecTreeItem {
    GObject       parent_instance;
    char         *name;
    char         *icon;       /* symbolic icon name */
    NodeType      node_type;
    IedModelNode *model_node; /* back-pointer (not owned) */
};

/* ---- GObject wrapper for data attribute rows (right pane) ---- */

#define IEC_TYPE_ROW_ITEM (iec_row_item_get_type())
G_DECLARE_FINAL_TYPE(IecRowItem, iec_row_item, IEC, ROW_ITEM, GObject)

struct _IecRowItem {
    GObject       parent_instance;
    char         *name;
    char         *value;
    char         *type_str;     /* CDC type string ("SPC", "MV", …)  */
    char         *mms_type;     /* "MmsFloat", "MmsBoolean", …       */
    char         *timestamp;
    char         *description;
    char         *fc;           /* functional constraint badge        */
    char         *badge_type;   /* "DO" / "DA" badge text             */
    NodeType      node_type;
    IedModelNode *model_node;   /* back-pointer for child expansion   */
};

/* ---- Application state ---- */

typedef struct {
    IedConnection   con;
    char           *host;
    int             port;
    IedModelNode   *model_root;
    gboolean        connected;
    GPtrArray      *active_rcbs;  /* ClientReportControlBlock* */
} BayConnection;

typedef struct {
    /* Substation Configuration */
    SubstationConfig sub_config;

    /* Explorer connection — driven by user IP/port input */
    IedConnection   explorer_con;
    gboolean        explorer_connected;

    /* SLD bay connections — auto-managed for telemetry */
    BayConnection   bays_con[SLD_MAX_BAYS];

    /* GTK widgets */
    GtkApplication *gtk_app;
    GtkWidget      *window;
    GtkWidget      *status_label;
    GtkWidget      *connect_btn;
    GtkWidget      *host_entry;
    GtkWidget      *port_entry;
    GtkWidget      *auto_poll_btn;
    GtkWidget      *write_btn;
    GtkWidget      *config_btn;

    /* Polling state */
    gboolean        auto_poll;
    guint           poll_timer_id;

    /* Currently selected DA for write operations */
    IedModelNode   *selected_da;

    /* Left pane — IED explorer tree */
    GtkWidget      *tree_view;
    GListStore     *tree_store;

    /* Right pane — Data attribute column view */
    GtkWidget      *col_view;
    GListStore     *data_store;

    /* Breadcrumb / path label */
    GtkWidget      *path_label;

    /* Currently selected LN for right pane */
    IedModelNode   *selected_ln;

    /* Single combined UI root for the tree view */
    IedModelNode   *ui_model_root;

    /* SLD state */
    GtkWidget      *sld_area;
    SldEquipment    sld_equipment[SLD_MAX_EQUIPMENT];
    int             sld_equipment_count;
} AppState;

/* ---- Async connection payload ---- */

typedef struct {
    AppState     *app;
    char          host[128];
    int           port;
    gboolean      success;
    IedModelNode *model;
} ConnectTaskData;

/* ---- iec61850_conn.h ---- */

void           app_connect          (AppState *app);
void           app_connect_async    (AppState *app, const char *host, int port, GAsyncReadyCallback callback, gpointer user_data);
void           discover_ln_children (IedConnection con, IedModelNode *lnNode);
void           app_disconnect       (AppState *app);
IedModelNode  *discover_model_con   (IedConnection con);
void           app_read_all_values  (AppState *app);
IedModelNode  *ied_model_node_new   (const char *name, const char *ref, NodeType type);
void           ied_model_node_free  (IedModelNode *node);

/* SLD bay connections (separate from explorer) */
void           sld_connect_bays     (AppState *app);
void           sld_disconnect_bays  (AppState *app);
void           app_reconnect_bays   (AppState *app);

/* Write feature */
typedef enum {
    CTL_ACTION_SELECT = 0,
    CTL_ACTION_OPERATE = 1,
    CTL_ACTION_SEL_OP = 2,
    CTL_ACTION_CANCEL = 3
} ControlAction;

gboolean       is_node_writable     (const char *fc);
void           app_write_direct     (AppState *app, int bay_index, IedModelNode *da, MmsValue *val);
void           app_control_operate  (AppState *app, int bay_index, const char *doRef, MmsValue *ctlVal, int ctlModel, ControlAction action);
void           app_control_cancel   (AppState *app, int bay_index, const char *doRef);

/* ---- iec61850_gui.h ---- */

void  app_gui_init           (AppState *app, GtkApplication *gtk_app);
void  populate_tree_view     (AppState *app);
void  populate_data_view     (AppState *app, IedModelNode *ln_node);
void  set_status             (AppState *app, const char *msg, const char *css_class);
void  show_control_dialog    (AppState *app, int bay_index, IedModelNode *da);
void  show_direct_write_dialog(AppState *app, int bay_index, IedModelNode *da);
void  show_substation_config_dialog(AppState *app, int initial_bay);

#endif /* IEC61850_APP_H */
