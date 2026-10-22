/* ============================================================
 *  main.c — Entry point for the IEC 61850 Client
 * ============================================================ */
#include "iec61850_app.h"

static AppState app = {0};

static void
on_activate(GtkApplication *gtk_app, gpointer user_data)
{
    (void)user_data;

    /* Load substation configuration (creates default if missing) */
    substation_config_load("config/substation_config.json", &app.sub_config);

    for (int i = 0; i < app.sub_config.num_bays && i < SLD_MAX_BAYS; i++) {
        app.bays_con[i].host = g_strdup(app.sub_config.bays[i].host);
        app.bays_con[i].port = app.sub_config.bays[i].port;
        app.bays_con[i].active_rcbs = g_ptr_array_new();
    }

    /* Initialize unified tree root */
    app.ui_model_root = NULL;

    app_gui_init(&app, gtk_app);
}

static void
on_shutdown(GtkApplication *gtk_app, gpointer user_data)
{
    (void)gtk_app; (void)user_data;

    app_disconnect(&app);

    for (int i = 0; i < app.sub_config.num_bays && i < SLD_MAX_BAYS; i++) {
        g_free(app.bays_con[i].host);
        if (app.bays_con[i].active_rcbs)
            g_ptr_array_unref(app.bays_con[i].active_rcbs);
    }
}

int
main(int argc, char **argv)
{
    GtkApplication *gtk_app = gtk_application_new(
        "com.goose.iec61850client", G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect(gtk_app, "activate", G_CALLBACK(on_activate), NULL);
    g_signal_connect(gtk_app, "shutdown", G_CALLBACK(on_shutdown), NULL);

    int status = g_application_run(G_APPLICATION(gtk_app), argc, argv);
    g_object_unref(gtk_app);
    return status;
}
