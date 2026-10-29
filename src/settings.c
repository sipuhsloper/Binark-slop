#include "settings.h"

#include <adwaita.h>

GSettings *
binark_settings_new(void)
{
    return g_settings_new(BINARK_SETTINGS_SCHEMA);
}

void
binark_settings_apply_theme(GSettings *settings)
{
    AdwStyleManager *manager = adw_style_manager_get_default();
    gchar *theme;

    if (settings == NULL)
        return;

    theme = g_settings_get_string(settings, "theme");

    if (g_strcmp0(theme, "light") == 0)
        adw_style_manager_set_color_scheme(manager, ADW_COLOR_SCHEME_FORCE_LIGHT);
    else if (g_strcmp0(theme, "dark") == 0)
        adw_style_manager_set_color_scheme(manager, ADW_COLOR_SCHEME_FORCE_DARK);
    else
        adw_style_manager_set_color_scheme(manager, ADW_COLOR_SCHEME_DEFAULT);

    g_free(theme);
}
