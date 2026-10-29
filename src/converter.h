#ifndef BINARK_CONVERTER_H
#define BINARK_CONVERTER_H

#include <glib.h>

G_BEGIN_DECLS

/* Human-readable size, localized by GLib. */
gchar *binark_format_size(guint64 bytes);

/* Count Unicode characters in a valid UTF-8 string. */
gsize binark_utf8_char_count(const gchar *text);

G_END_DECLS

#endif /* BINARK_CONVERTER_H */
