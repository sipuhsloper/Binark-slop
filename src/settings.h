#ifndef BINARK_SETTINGS_H
#define BINARK_SETTINGS_H

#include <gio/gio.h>
#include <glib.h>

G_BEGIN_DECLS

#define BINARK_SETTINGS_SCHEMA "com.binark.Binark"
#define BINARK_APPLICATION_ID "com.binark.Binark"
#define BINARK_APPLICATION_NAME "Binark"
#define BINARK_VERSION "1.0.0"

GSettings *binark_settings_new(void);
void binark_settings_apply_theme(GSettings *settings);

G_END_DECLS

#endif /* BINARK_SETTINGS_H */
