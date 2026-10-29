#include <adwaita.h>
#include <glib.h>

#include "binark-resources.h"
#include "settings.h"
#include "window.h"

static void
ensure_development_schema_dir(void)
{
    gchar *executable;
    gchar *directory;
    gchar *schema_dir;
    gchar *compiled;

    if (g_getenv("GSETTINGS_SCHEMA_DIR") != NULL)
        return;

    executable = g_file_read_link("/proc/self/exe", NULL);
    if (executable == NULL)
        return;

    directory = g_path_get_dirname(executable);
    schema_dir = g_build_filename(directory, "gschemas", NULL);
    compiled = g_build_filename(schema_dir, "gschemas.compiled", NULL);

    if (g_file_test(compiled, G_FILE_TEST_IS_REGULAR))
        g_setenv("GSETTINGS_SCHEMA_DIR", schema_dir, TRUE);

    g_free(compiled);
    g_free(schema_dir);
    g_free(directory);
    g_free(executable);
}

static void
load_resources(void)
{
    static gboolean loaded = FALSE;
    GdkDisplay *display;
    GtkIconTheme *icon_theme;
    GtkCssProvider *provider;

    if (loaded)
        return;
    loaded = TRUE;

    g_resources_register(binark_get_resource());

    display = gdk_display_get_default();
    if (display == NULL)
        return;

    icon_theme = gtk_icon_theme_get_for_display(display);
    gtk_icon_theme_add_resource_path(icon_theme, "/com/binark/Binark/icons");

    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, "/com/binark/Binark/style.css");
    gtk_style_context_add_provider_for_display(
        display, GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void
activate(GtkApplication *application, gpointer user_data)
{
    GList *windows;
    GtkWindow *window;

    (void)user_data;
    load_resources();

    windows = gtk_application_get_windows(application);
    if (windows != NULL) {
        window = GTK_WINDOW(windows->data);
        gtk_window_present(window);
        return;
    }

    window = GTK_WINDOW(binark_window_new(application));
    gtk_window_present(window);
}

int
main(int argc, char **argv)
{
    AdwApplication *application;
    int status;

    ensure_development_schema_dir();
    g_set_application_name(BINARK_APPLICATION_NAME);

    application = adw_application_new(BINARK_APPLICATION_ID,
                                      G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(activate), NULL);

    status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    return status;
}
