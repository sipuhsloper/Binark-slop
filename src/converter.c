#include "converter.h"

#include <string.h>

gchar *
binark_format_size(guint64 bytes)
{
    if (bytes < 1024)
        return g_strdup_printf("%" G_GUINT64_FORMAT " Б", bytes);

    return g_format_size_full(bytes, G_FORMAT_SIZE_IEC_UNITS);
}

gsize
binark_utf8_char_count(const gchar *text)
{
    const gchar *cursor;
    gsize count = 0;

    if (text == NULL || *text == '\0')
        return 0;

    for (cursor = text; *cursor != '\0'; cursor = g_utf8_next_char(cursor))
        count++;

    return count;
}
